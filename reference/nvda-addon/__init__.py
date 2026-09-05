import os
import sys
sys.path.append(os.path.join(os.path.dirname(__file__), "orpheus"))
import subprocess
import threading
import json
import re
from io import StringIO
import struct
import time
from collections import namedtuple
import shutil
import socket
import winreg

from synthDriverHandler import synthDoneSpeaking, SynthDriver, synthIndexReached, VoiceInfo
from speech.commands import PitchCommand, IndexCommand, LangChangeCommand
from autoSettingsUtils.driverSetting import NumericDriverSetting
import addonHandler
import config
import nvwave
import array

addonHandler.initTranslation()

METADATA_COMMAND_TIMEOUT = 1.0
HOST_CONNECT_TIMEOUT = 10.0
HOST_INIT_TIMEOUT = 30.0
DEFAULT_LANGUAGE = "en-gb"
FALLBACK_PITCH_MIN = 50
FALLBACK_PITCH_MAX = 500
FALLBACK_DEFAULT_PITCH = 110
CONF_SECTION = "orpheusNative"
CONF_PARAMETER_OVERRIDES = "parameterOverrides"
DATA_DIR_NAME = "orpheusNative"
DICTIONARY_DIR_NAME = "dictionaries"
DEFAULT_DICTIONARY_FILE_NAME = "defaultDictionary.json"
DEFAULT_DICTIONARY_DIR_NAME = "defaultDictionaries"
DEFAULT_EXCEPTION_LANGUAGE = 44
OBSOLETE_DICTIONARY_ENTRIES = {
	("beige", "_b_ai_zh"),
	("beige", "_b_ei_zh"),
}
EXCEPTION_MATCH_ANYWHERE = "anywhere"
EXCEPTION_MATCH_WHOLE_WORD = "wholeWord"
EXCEPTION_MATCH_REGEX = "regularExpression"
EXCEPTION_MATCH_STARTS_WITH = "startsWith"
EXCEPTION_MATCH_ENDS_WITH = "endsWith"
EXCEPTION_MATCH_TYPES = {
	EXCEPTION_MATCH_ANYWHERE,
	EXCEPTION_MATCH_WHOLE_WORD,
	EXCEPTION_MATCH_REGEX,
	EXCEPTION_MATCH_STARTS_WITH,
	EXCEPTION_MATCH_ENDS_WITH,
}
VOICE_DESC_INSTALLED_KEY = r"Software\Dolphin\Orpheus210\Voices\Installed"
VOICE_DESC_USER_KEY = r"Software\Dolphin\Orpheus210\Voices\User Defined"
VOICE_DESC_VALUE = "VoiceDesc"
VOICE_DESC_RECORD_SIZE = 284
VOICE_DESC_PATH_OFFSET = 20
VOICE_DESC_INTONATION_OFFSET = 0xF8
VOICE_DESC_HEAD_SIZE_OFFSET = 0x104
VOICE_DESC_VOICING_OFFSET = 0x108
ORPHEUS_REGISTRY_READ_ACCESS = winreg.KEY_READ | getattr(winreg, "KEY_WOW64_32KEY", 0)
ORPHEUS_REGISTRY_WRITE_ACCESS = winreg.KEY_SET_VALUE | getattr(winreg, "KEY_WOW64_32KEY", 0)
PARAMETER_SETTINGS = (
	("skimReadingLevel", ("skim",), _("Skim reading &level"), 0, 8, 0, 1, 1, 2, _("Skim reading level"), 2),
	("wordPauseAmount", ("word", "pause"), _("&Word pause"), 0, 100, 0, 1, 5, 10, _("Word pause"), 3),
	("phrasePauseAmount", ("phrase", "pause"), _("P&hrase pause"), 0, 100, 12, 1, 5, 10, _("Phrase pause"), 4),
)
LANGUAGES = {
	1: 'en',
	44: 'en-gb',
	30: 'el',
	31: 'nl',
	33: 'fr',
	34: 'es',
	36: 'hu',
	38: 'hr',
	39: 'it',
	40: 'ro',
	42: 'cs',
	45: 'da',
	46: 'sv',
	47: 'nb_NO',
	48: 'pl',
	49: 'de',
	52: 'es-mx',
	55: 'pt-br',
	60: 'ms',
	86: 'zh',
	351: 'pt-pt',
	358: 'fi',
	370: 'lt',
	10044: 'cy',
	10086: 'zh',
}

ParamDesc = namedtuple('ParamDesc', ['min', 'max', 'name', 'current'])

FRAME_COMMAND = 1
FRAME_RESPONSE = 2
FRAME_EVENT = 3
CMD_INITIALIZE = 1
CMD_APPEND = 2
CMD_SPEAK_APPEND = 3
CMD_MUTE = 4
CMD_CONFIG = 5
CMD_GET_PARAMS = 6
CMD_GET_LANGS = 7
CMD_GET_VOICES = 8
CMD_CLOSE = 9
EV_AUDIO = 1
EV_DONE = 2
STATUS_OK = 0

COMMAND_IDS = {
	"initialize": CMD_INITIALIZE,
	"append": CMD_APPEND,
	"speakAppend": CMD_SPEAK_APPEND,
	"mute": CMD_MUTE,
	"config": CMD_CONFIG,
	"getParams": CMD_GET_PARAMS,
	"getLangs": CMD_GET_LANGS,
	"getVoices": CMD_GET_VOICES,
	"close": CMD_CLOSE,
}

def _ensure_native_exception_config():
	if CONF_SECTION not in config.conf.spec:
		config.conf.spec[CONF_SECTION] = {}
	config.conf.spec[CONF_SECTION].setdefault(CONF_PARAMETER_OVERRIDES, "string(default='{}')")
	_ = config.conf[CONF_SECTION]


def _native_data_dir():
	base = getattr(config, "getUserDefaultConfigPath", None)
	if callable(base):
		config_path = base()
	else:
		config_path = getattr(config, "confDir", None) or os.path.join(os.path.expanduser("~"), "AppData", "Roaming", "nvda")
	return os.path.join(config_path, DATA_DIR_NAME)


def _native_dictionary_path(language):
	return os.path.join(
		_native_data_dir(),
		DICTIONARY_DIR_NAME,
		f"{int(language):05d}.json",
	)


def _bundled_dictionary_path(language):
	path = os.path.join(
		os.path.dirname(__file__),
		DEFAULT_DICTIONARY_DIR_NAME,
		f"{int(language):05d}.json",
	)
	if os.path.exists(path):
		return path
	if int(language) == DEFAULT_EXCEPTION_LANGUAGE:
		return os.path.join(os.path.dirname(__file__), DEFAULT_DICTIONARY_FILE_NAME)
	return path


def _load_native_exceptions(language):
	try:
		_ensure_native_exception_config()
	except Exception:
		return []
	path = _native_dictionary_path(language)
	if not os.path.exists(path):
		path = _bundled_dictionary_path(language)
		if not os.path.exists(path):
			return []
	try:
		with open(path, "r", encoding="utf-8-sig") as f:
			data = json.load(f)
	except Exception:
		return []
	if isinstance(data, dict):
		items = data.get("entries", [])
	else:
		items = data
	if not isinstance(items, list):
		return []
	entries = []
	seen = set()
	for item in items:
		if not isinstance(item, dict):
			continue
		entry = _normalise_native_exception(item)
		if entry is not None:
			key = (
				entry["source"].casefold(),
				entry["replacement"].casefold(),
				entry["matchType"],
				entry["caseSensitive"],
			)
			if key in seen:
				continue
			seen.add(key)
			entries.append(entry)
	return entries


def _normalise_native_exception(item):
	if not isinstance(item, dict):
		return None
	source = str(item.get("source", "") or "").strip()
	replacement = str(item.get("replacement", "") or "")
	if (source.casefold(), replacement.casefold()) in OBSOLETE_DICTIONARY_ENTRIES:
		return None
	match_type = str(item.get("matchType", EXCEPTION_MATCH_WHOLE_WORD) or EXCEPTION_MATCH_WHOLE_WORD)
	if match_type not in EXCEPTION_MATCH_TYPES:
		match_type = EXCEPTION_MATCH_WHOLE_WORD
	case_sensitive = bool(item.get("caseSensitive", False))
	if not source:
		return None
	return {
		"source": source,
		"replacement": replacement,
		"matchType": match_type,
		"caseSensitive": case_sensitive,
	}


def _build_orpheus_settings():
	settings = [
		SynthDriver.RateSetting(),
		SynthDriver.VolumeSetting(),
		SynthDriver.PitchSetting(),
		NumericDriverSetting(
			"intonation",
			_("&Intonation"),
			availableInSettingsRing=True,
			defaultVal=50,
			minVal=0,
			maxVal=100,
			minStep=1,
			normalStep=5,
			largeStep=10,
			displayName=_("Intonation"),
		),
		NumericDriverSetting(
			"headSize",
			_("&Head size"),
			availableInSettingsRing=True,
			defaultVal=50,
			minVal=0,
			maxVal=100,
			minStep=1,
			normalStep=5,
			largeStep=10,
			displayName=_("Head size"),
		),
		NumericDriverSetting(
			"voicing",
			_("Voicin&g"),
			availableInSettingsRing=True,
			defaultVal=100,
			minVal=0,
			maxVal=100,
			minStep=1,
			normalStep=5,
			largeStep=10,
			displayName=_("Voicing"),
		),
		SynthDriver.VoiceSetting(),
		SynthDriver.VariantSetting(),
	]
	for key, _tokens, label, min_value, max_value, default, min_step, normal_step, large_step, display_name, _fallback_id in PARAMETER_SETTINGS:
		settings.append(NumericDriverSetting(
			key,
			label,
			availableInSettingsRing=True,
			defaultVal=default,
			minVal=min_value,
			maxVal=max_value,
			minStep=min_step,
			normalStep=normal_step,
			largeStep=large_step,
			displayName=display_name,
		))
	return tuple(settings)


def _parameter_commands_from_synth(synth):
	result = []
	for key, _tokens, _label, min_value, max_value, default, _min_step, _normal_step, _large_step, _display_name, fallback_id in PARAMETER_SETTINGS:
		value = synth._clampParam(getattr(synth, key, default), min_value, max_value)
		value = synth._parameterSettingToOrpheusValue(key, value)
		param_id = synth._parameterIdFor(key, fallback_id)
		result.append((0, 0, param_id, value))
	return result

class SynthDriver(SynthDriver):
	supportedSettings = _build_orpheus_settings()
	supportedCommands = {
		IndexCommand,
		PitchCommand,
		LangChangeCommand,
	}

	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	name='orpheusNative'
	description='Orpheus Native'

	@classmethod
	def check(cls):
		return True

	def __init__(self):
		self.lock = threading.Lock()
		self.queue = []
		self._response_events = {}
		self._next_msg_id = 1
		self._msg_lock = threading.Lock()
		self._send_lock = threading.Lock()
		self._process = None
		self._stop_reader = False
		self._langs = []
		self._voicesByCountry = {}
		self._initialIntonationConfigUntil = time.time() + 5
		
		self.event = threading.Event()
		
		try:
			output = config.conf["audio"]["outputDevice"]
		except:
			output = config.conf["speech"]["outputDevice"]
		self.player = nvwave.WavePlayer(1, 22050, 16, outputDevice=output)
		
		self.is_speaking = False
		self._last_audio_time = 0
		self._fallback_done_timer = None
		self._pendingFinalIndex = 0
		
		self._start_host()
		
		# Cache Orpheus parameter descriptors (used for mapping NVDA percentages to
		# Orpheus parameter values).
		#
		# Important: Do NOT force the synth to Orpheus' internal default pitch here.
		# NVDA will apply the user's saved synth settings immediately after the driver
		# is instantiated. Setting pitch based on Orpheus' current value can override
		# NVDA's intended defaults and makes the pitch slider feel "ignored".
		self._paramDescs = self.get_params() or []
		self._pitchDesc = self._paramDescs[1] if len(self._paramDescs) > 1 else None
		self._pitchMin = self._pitchDesc.min if self._pitchDesc else FALLBACK_PITCH_MIN
		self._pitchMax = self._pitchDesc.max if self._pitchDesc else FALLBACK_PITCH_MAX
		self._defaultPitch = self._get_default_pitch()
		self._refresh_parameter_ids()
		self._refresh_language_cache()
		
		# Defaults (NVDA will override from config where applicable).
		self.volume = 100
		self.pitch = 50
		self._intonation = self._read_current_voice_intonation()
		self._headSize = self._read_current_voice_head_size()
		self._voicing = self._read_current_voice_voicing()
			
		self.rate = 50
		self.voice = 0
		self.variant = 0
		for key, _tokens, _label, _min_value, _max_value, default, _min_step, _normal_step, _large_step, _display_name, _fallback_id in PARAMETER_SETTINGS:
			setattr(self, "_" + key, default)
		self.index = 0
		self.last_reached = 0

		# Background worker for speech commands
		self._work_event = threading.Event()
		self._worker_thread = threading.Thread(target=self._worker_loop)
		self._worker_thread.daemon = True
		self._worker_thread.start()

	def initSettings(self):
		super().initSettings()
		self._migrateLegacyPauseSettings()

	def _start_host(self):
		host_exe = os.path.join(os.path.dirname(__file__), "orpheus-native-host.exe")
		if not os.path.exists(host_exe):
			raise RuntimeError("Native Orpheus host not found: " + host_exe)

		server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
		server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		server.bind(("127.0.0.1", 0))
		server.listen(1)
		server.settimeout(HOST_CONNECT_TIMEOUT)
		address = server.getsockname()

		cmd = [host_exe, "--address", f"{address[0]}:{address[1]}"]
		
		startupinfo = subprocess.STARTUPINFO()
		startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
		
		self._process = subprocess.Popen(cmd, startupinfo=startupinfo)
		
		try:
			# Wait for connection with timeout
			self._conn, _peer = server.accept()
			self._conn.settimeout(1.0)
		except Exception:
			# If connection fails, ensure process is killed and re-raise
			if self._process.poll() is None:
				self._process.terminate()
			server.close()
			raise RuntimeError("Failed to connect to Orpheus host process.")
			
		server.close()
		
		self._reader_thread = threading.Thread(target=self._read_loop)
		self._reader_thread.start()
		
		orpheus_dir = os.path.join(os.path.dirname(__file__), 'orpheus')
		try:
			self._send_command("initialize", timeout=HOST_INIT_TIMEOUT, orpheus_dir=orpheus_dir)
		except RuntimeError:
			# If initialization fails, cleanup
			self.terminate()
			raise

	def _send_command(self, command, timeout=None, **payload):
		command_id = COMMAND_IDS[command]
		with self._msg_lock:
			msg_id = self._next_msg_id
			self._next_msg_id += 1
			evt = threading.Event()
			self._response_events[msg_id] = (evt, None)
		
		try:
			self._send_frame(self._build_command_frame(msg_id, command_id, payload))
			if not evt.wait(timeout):
				with self._msg_lock:
					self._response_events.pop(msg_id, None)
				raise RuntimeError("Timed out waiting for Orpheus host command: %s" % command)
			with self._msg_lock:
				try:
					_, response = self._response_events.pop(msg_id)
				except KeyError:
					# This can happen if cleanup removed it (though we stopped doing that)
					# or if something else went wrong.
					raise RuntimeError("Connection lost (event missing)")
			
			if isinstance(response, Exception):
				raise response
			return self._parse_response(command, response or b"")
		except Exception:
			# If sending fails, we should probably cleanup
			with self._msg_lock:
				if msg_id in self._response_events:
					del self._response_events[msg_id]
			raise

	def _build_command_frame(self, msg_id, command_id, payload):
		data = b""
		if command_id == CMD_INITIALIZE:
			path = payload["orpheus_dir"].encode("utf-8")
			data = struct.pack("<I", len(path)) + path
		elif command_id == CMD_APPEND:
			params = payload["parameters_bytes"]
			text = payload["text_bytes"]
			data = struct.pack("<II", len(params), len(text)) + params + text
		elif command_id == CMD_MUTE:
			data = struct.pack("<I", int(payload["val"]))
		elif command_id == CMD_GET_VOICES:
			data = struct.pack("<I", int(payload["country"]))
		return struct.pack("<BIH", FRAME_COMMAND, msg_id, command_id) + data

	def _send_frame(self, payload):
		frame = struct.pack("<I", len(payload)) + payload
		with self._send_lock:
			self._conn.sendall(frame)

	def _read_utf16z(self, data, offset, end, max_chars=80):
		limit = min(offset + max_chars * 2, end)
		chunks = []
		for pos in range(offset, limit, 2):
			ch = data[pos:pos + 2]
			if ch == b"\x00\x00":
				break
			chunks.append(ch)
		return b"".join(chunks).decode("utf-16le", "replace")

	def _voice_desc_record_offsets(self, data):
		offsets = []
		for pos in range(0, len(data) - VOICE_DESC_PATH_OFFSET):
			if data[pos + 16:pos + 20] != b"\x00" * 4:
				continue
			path = self._read_utf16z(data, pos + VOICE_DESC_PATH_OFFSET, min(len(data), pos + VOICE_DESC_RECORD_SIZE))
			if len(path) >= 7 and path[:5].isdigit() and path[5] == "\\":
				offsets.append(pos)
		return offsets

	def _voice_desc_data(self, key_path):
		with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0, ORPHEUS_REGISTRY_READ_ACCESS) as key:
			data, reg_type = winreg.QueryValueEx(key, VOICE_DESC_VALUE)
		if reg_type != winreg.REG_BINARY:
			raise ValueError("Orpheus VoiceDesc is not a binary registry value")
		return bytearray(data)

	def _write_voice_desc_data(self, key_path, data):
		with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0, ORPHEUS_REGISTRY_WRITE_ACCESS) as key:
			winreg.SetValueEx(key, VOICE_DESC_VALUE, 0, winreg.REG_BINARY, bytes(data))

	def _current_voice_desc_offset(self, data):
		langs = self._get_cached_langs()
		if not langs:
			self._refresh_language_cache()
			langs = self._get_cached_langs()
		try:
			country = int(langs[int(getattr(self, "_voice", 0))]["country"])
		except Exception:
			return None
		prefix = "%05d\\" % country
		matches = []
		offsets = self._voice_desc_record_offsets(data)
		for index, offset in enumerate(offsets):
			end = offsets[index + 1] if index + 1 < len(offsets) else min(len(data), offset + VOICE_DESC_RECORD_SIZE)
			path = self._read_utf16z(data, offset + VOICE_DESC_PATH_OFFSET, end)
			if path.lower().startswith(prefix.lower()):
				matches.append(offset)
		try:
			variant = int(getattr(self, "_variant", 0))
		except Exception:
			variant = 0
		if 0 <= variant < len(matches):
			return matches[variant]
		return matches[0] if matches else None

	def _read_current_voice_attribute(self, field_offset, min_value, max_value, default):
		for key_path in (VOICE_DESC_USER_KEY, VOICE_DESC_INSTALLED_KEY):
			try:
				data = self._voice_desc_data(key_path)
				offset = self._current_voice_desc_offset(data)
				if offset is None:
					continue
				return self._clampParam(
					struct.unpack_from("<i", data, offset + field_offset)[0],
					min_value,
					max_value,
				)
			except Exception:
				continue
		return default

	def _write_current_voice_attribute(self, field_offset, value, min_value, max_value):
		value = self._clampParam(value, min_value, max_value)
		wrote = False
		for key_path in (VOICE_DESC_USER_KEY, VOICE_DESC_INSTALLED_KEY):
			try:
				data = self._voice_desc_data(key_path)
				offset = self._current_voice_desc_offset(data)
				if offset is None:
					continue
				struct.pack_into("<i", data, offset + field_offset, value)
				self._write_voice_desc_data(key_path, data)
				wrote = True
			except Exception:
				continue
		if not wrote:
			raise ValueError("Current Orpheus voice was not found in VoiceDesc")

	def _headSizeRawToSetting(self, value):
		return self._paramToPercent(self._clampParam(value, -100, 100), -100, 100)

	def _headSizeSettingToRaw(self, value):
		return self._percentToParam(self._clampPercent(value), -100, 100)

	def _read_current_voice_intonation(self):
		return self._read_current_voice_attribute(VOICE_DESC_INTONATION_OFFSET, 0, 100, 50)

	def _write_current_voice_intonation(self, value):
		self._write_current_voice_attribute(VOICE_DESC_INTONATION_OFFSET, value, 0, 100)

	def _read_current_voice_head_size(self):
		raw = self._read_current_voice_attribute(VOICE_DESC_HEAD_SIZE_OFFSET, -100, 100, 0)
		return self._headSizeRawToSetting(raw)

	def _write_current_voice_head_size(self, value):
		self._write_current_voice_attribute(
			VOICE_DESC_HEAD_SIZE_OFFSET,
			self._headSizeSettingToRaw(value),
			-100,
			100,
		)

	def _read_current_voice_voicing(self):
		return self._read_current_voice_attribute(VOICE_DESC_VOICING_OFFSET, 0, 100, 100)

	def _write_current_voice_voicing(self, value):
		self._write_current_voice_attribute(VOICE_DESC_VOICING_OFFSET, value, 0, 100)

	def _recv_exact(self, length):
		chunks = []
		remaining = length
		while remaining:
			try:
				chunk = self._conn.recv(remaining)
			except socket.timeout:
				if self._stop_reader:
					return None
				continue
			if not chunk:
				return None
			chunks.append(chunk)
			remaining -= len(chunk)
		return b"".join(chunks)

	def _recv_frame(self):
		header = self._recv_exact(4)
		if not header:
			return None
		length = struct.unpack("<I", header)[0]
		if length <= 0 or length > 64 * 1024 * 1024:
			return None
		return self._recv_exact(length)

	def _read_loop(self):
		while not self._stop_reader:
			try:
				frame = self._recv_frame()
				if frame is None:
					break
				frame_type = frame[0]
				if frame_type == FRAME_RESPONSE:
					msg_id, status = struct.unpack_from("<II", frame, 1)
					payload = frame[9:]
					with self._msg_lock:
						if msg_id in self._response_events:
							evt, _ = self._response_events[msg_id]
							if status != STATUS_OK:
								self._response_events[msg_id] = (evt, RuntimeError(payload.decode("utf-8", "replace")))
							else:
								self._response_events[msg_id] = (evt, payload)
							evt.set()
				elif frame_type == FRAME_EVENT:
					event = struct.unpack_from("<H", frame, 1)[0]
					self._handle_event(event, frame[3:])
			except Exception:
				# Connection broken or error
				break
		
		# Cleanup pending events to avoid deadlocks
		with self._msg_lock:
			for msg_id, (evt, _) in self._response_events.items():
				self._response_events[msg_id] = (evt, RuntimeError("Connection lost"))
				evt.set()
			# Do not clear, let the waiters pop their events

	def _handle_event(self, event, payload):
		if event == EV_AUDIO:
			offset = 0
			audio_len = struct.unpack_from("<I", payload, offset)[0]
			offset += 4
			audio_data = payload[offset:offset + audio_len]
			offset += audio_len
			controls_len = struct.unpack_from("<I", payload, offset)[0]
			offset += 4
			control_data = payload[offset:offset + controls_len]
			controls = list(struct.iter_unpack('III', control_data)) if controls_len else []
			self._on_audio({"audio": audio_data, "controls": controls})

	def _parse_response(self, command, payload):
		if command in ("initialize", "append", "speakAppend", "mute", "config", "close"):
			return "ok"
		if command == "getParams":
			return self._parse_params(payload)
		if command == "getLangs":
			return self._parse_langs(payload)
		if command == "getVoices":
			return self._parse_voices(payload)
		return payload

	def _read_string(self, payload, offset):
		length = struct.unpack_from("<I", payload, offset)[0]
		offset += 4
		value = payload[offset:offset + length].decode("utf-8", "replace")
		return value, offset + length

	def _parse_params(self, payload):
		offset = 0
		count = struct.unpack_from("<I", payload, offset)[0]
		offset += 4
		result = []
		for _i in range(count):
			minv, maxv, current = struct.unpack_from("<iii", payload, offset)
			offset += 12
			name, offset = self._read_string(payload, offset)
			result.append({"min": minv, "max": maxv, "name": name, "current": current})
		return result

	def _parse_langs(self, payload):
		offset = 0
		count = struct.unpack_from("<I", payload, offset)[0]
		offset += 4
		result = []
		for _i in range(count):
			country = struct.unpack_from("<I", payload, offset)[0]
			offset += 4
			lang, offset = self._read_string(payload, offset)
			result.append({"country": country, "lang": lang})
		return result

	def _parse_voices(self, payload):
		offset = 0
		count = struct.unpack_from("<I", payload, offset)[0]
		offset += 4
		result = []
		for _i in range(count):
			name, offset = self._read_string(payload, offset)
			result.append({"name": name})
		return result

	def _on_audio(self, payload):
		audio_data = payload["audio"]
		controls = payload["controls"]
		
		if not self.is_speaking:
			self.event.set()
			return
			
		# Apply volume (NVDA percentage) in the driver, since Orpheus' API doesn't
		# expose a reliable per-utterance volume parameter.
		vol = getattr(self, "_volume", 100)
		if vol <= 0:
			audio_data = b"\x00" * len(audio_data)
		elif vol < 100:
			factor = vol / 100.0
			samples = array.array('h')
			samples.frombytes(audio_data)
			for i, s in enumerate(samples):
				v = int(s * factor)
				if v > 32767:
					v = 32767
				elif v < -32768:
					v = -32768
				samples[i] = v
			audio_data = samples.tobytes()

		self.player.feed(audio_data)
		self._last_audio_time = time.time()
		
		for pos, type, value in controls:
			if value & 0x80000000:
				index = value & 0x7FFFFFFF
				if index:
					synthIndexReached.notify(synth=self, index=index)
				self.player.idle()
				synthDoneSpeaking.notify(synth=self)
				self.is_speaking = False
				# Trigger worker to process next item
				self._work_event.set()
			else:
				synthIndexReached.notify(synth=self, index=value)
		if not controls:
			self._schedule_fallback_done()

	def _schedule_fallback_done(self):
		if self._fallback_done_timer is not None:
			self._fallback_done_timer.cancel()
		last_audio_time = self._last_audio_time
		self._fallback_done_timer = threading.Timer(2.0, self._fallback_done, args=(last_audio_time,))
		self._fallback_done_timer.daemon = True
		self._fallback_done_timer.start()

	def _fallback_done(self, audio_time):
		with self.lock:
			if not self.is_speaking or audio_time != self._last_audio_time:
				return
			self.is_speaking = False
			if self._fallback_done_timer is not None:
				self._fallback_done_timer.cancel()
				self._fallback_done_timer = None
			index = getattr(self, "_pendingFinalIndex", 0)
		if index:
			synthIndexReached.notify(synth=self, index=index)
		self.player.idle()
		synthDoneSpeaking.notify(synth=self)
		self._work_event.set()

	def speak(self, seq):
		# Ensure self._pitch and others are initialized. 
		# Although initialized in __init__ and updated in setters, extra safety doesn't hurt.
		# But since we fixed _set_pitch, relying on attributes is safe.
		
		parameters = [
			(0, 0, 0, int(self._rate)),
			(0, 0, 12, int(self._voice)),
			(0, 0, 7, int(self._variant)),
			(0, 0, 1, int(self._pitch)),
		]
		parameters.extend(_parameter_commands_from_synth(self))
		text = StringIO()
		text.write(' ')
		lastindex = 0
		activeInlinePitch = None
		activeDictionaryLanguage = self._countryForVoice(self._voice)
		for item in seq:
			if isinstance(item, str):
				text.write(self._apply_native_exceptions(item, activeDictionaryLanguage) + ' ')
			elif isinstance(item, IndexCommand):
				parameters += [(text.tell() - 1, 0, 14, item.index)]
				text.write(' ')
				lastindex = item.index
			elif isinstance(item, LangChangeCommand):
				languageVoice, languageVariant = self._voiceVariantForLang(getattr(item, "lang", None))
				activeDictionaryLanguage = self._countryForVoice(languageVoice)
				languageOffset = text.tell() - 1
				parameters += [
					(languageOffset, 0, 12, int(languageVoice)),
					(languageOffset, 0, 7, int(languageVariant)),
				]
				if activeInlinePitch is not None:
					self._replaceParamAtOffset(
						parameters,
						max(1, text.tell()),
						1,
						activeInlinePitch,
					)
				text.write(' ')
			elif isinstance(item, PitchCommand):
				if getattr(item, "isDefault", False):
					activeInlinePitch = None
					continue
				# NVDA sends inline pitch changes as synth-parameter offsets.
				# Remapping them through the 0..100 slider range makes capitals
				# jump far too high; keep the 2025-era Orpheus behavior.
				pitchValue = int(self._inlinePitchCommandToParam(item))
				activeInlinePitch = pitchValue
				pitchOffset = max(1, text.tell())
				self._replaceParamAtOffset(
					parameters,
					pitchOffset,
					1,
					pitchValue,
				)
				text.write(' ')

		parameters.append((text.tell() - 1, 0, 14, lastindex | 0x80000000))
		self._pendingFinalIndex = lastindex
		
		with self.lock:
			self.queue.append((parameters, text.getvalue()))
		
		self._work_event.set()

	def _worker_loop(self):
		while not self._stop_reader:
			self._work_event.wait()
			if self._stop_reader:
				break
			
			item_to_process = None
			
			with self.lock:
				if self.is_speaking:
					# Still speaking, wait for completion signal
					self._work_event.clear()
					continue
				
				if self.queue:
					item_to_process = self.queue.pop(0)
					self.is_speaking = True
				else:
					# Queue empty
					self._work_event.clear()
			
			if item_to_process:
				params, txt = item_to_process
				try:
					self.append(params, txt)
					self.speak_append()
				except Exception:
					# If speak fails, reset speaking state so we don't hang
					with self.lock:
						self.is_speaking = False

	table = {
		ord("’"): ord("'"),
		ord("“"): ord('"'),
		ord("”"): ord('"'),
	}

	def _apply_native_exceptions(self, text, language=None):
		if not text:
			return text
		language = language if language is not None else self._countryForVoice(self._voice)
		for entry in _load_native_exceptions(language):
			source = entry["source"]
			replacement = entry["replacement"]
			match_type = entry["matchType"]
			flags = 0 if entry["caseSensitive"] else re.IGNORECASE
			if match_type == EXCEPTION_MATCH_REGEX:
				pattern = source
			else:
				pattern = re.escape(source)
				if match_type == EXCEPTION_MATCH_WHOLE_WORD:
					pattern = r"(?<!\w)%s(?!\w)" % pattern
				elif match_type == EXCEPTION_MATCH_STARTS_WITH:
					pattern = r"(?<!\w)%s" % pattern
				elif match_type == EXCEPTION_MATCH_ENDS_WITH:
					pattern = r"%s(?!\w)" % pattern
			try:
				text = re.sub(pattern, lambda _match, repl=replacement: repl, text, flags=flags)
			except Exception:
				continue
		return text

	def append(self, parameters, text):
		text = text.translate(self.table)
		param_str = b"".join(struct.pack('4I', *x) for x in parameters)
		text_bytes = text.encode('utf-16le')
		self._send_command("append", parameters_bytes=param_str, text_bytes=text_bytes)

	def speak_append(self):
		self._send_command("speakAppend")

	def _set_rate(self, rate):
		self._rate = self._percentToParam(rate, 10, 700)

	def _get_rate(self):
		return self._paramToPercent(self._rate, 10, 700)

	def _clampPercent(self, value):
		"""Clamp a value to NVDA's standard 0..100 percent range."""
		try:
			v = int(value)
		except Exception:
			v = 0
		return max(0, min(100, v))

	def _clampParam(self, value, minv, maxv):
		try:
			v = int(round(value))
		except Exception:
			v = minv
		return max(minv, min(maxv, v))

	def _pauseSettingToOrpheusMs(self, value, maxMs):
		value = self._clampPercent(value)
		return self._clampParam((value / 100.0) * maxMs, 0, maxMs)

	def _legacyPauseMsToSetting(self, value, maxMs):
		value = self._clampParam(value, 0, maxMs)
		return self._clampPercent(round((value / float(maxMs)) * 100))

	def _coercePauseSetting(self, value, maxMs):
		try:
			v = int(round(value))
		except Exception:
			return 0
		if v > 100:
			return self._legacyPauseMsToSetting(v, maxMs)
		return self._clampPercent(v)

	def _parameterSettingToOrpheusValue(self, key, value):
		if key == "wordPauseAmount":
			return self._pauseSettingToOrpheusMs(value, 1000)
		if key == "phrasePauseAmount":
			return self._pauseSettingToOrpheusMs(value, 2000)
		return value

	def _migrateLegacyPauseSettings(self):
		try:
			conf = config.conf["speech"][self.name]
		except Exception:
			return
		try:
			if conf.get("wordPauseAmount") is None and conf.get("wordPause") is not None:
				self.wordPauseAmount = self._legacyPauseMsToSetting(conf["wordPause"], 1000)
				conf["wordPauseAmount"] = self.wordPauseAmount
			if conf.get("phrasePauseAmount") is None and conf.get("phrasePause") is not None:
				self.phrasePauseAmount = self._legacyPauseMsToSetting(conf["phrasePause"], 2000)
				conf["phrasePauseAmount"] = self.phrasePauseAmount
		except Exception:
			pass

	def _get_default_pitch(self):
		desc = getattr(self, "_pitchDesc", None)
		if desc is not None and desc.current:
			return self._clampParam(desc.current, desc.min, desc.max)
		return self._clampParam(FALLBACK_DEFAULT_PITCH, self._pitchMin, self._pitchMax)

	def _pitchPercentToParam(self, pct):
		"""Convert an NVDA pitch percentage (0..100) to Orpheus pitch parameter."""
		pct = self._clampPercent(pct)
		minv = getattr(self, "_pitchMin", FALLBACK_PITCH_MIN)
		maxv = getattr(self, "_pitchMax", FALLBACK_PITCH_MAX)
		pivot = self._clampParam(getattr(self, "_defaultPitch", FALLBACK_DEFAULT_PITCH), minv, maxv)

		if pct <= 50:
			val = minv + (pivot - minv) * (pct / 50.0)
		else:
			val = pivot + (maxv - pivot) * ((pct - 50) / 50.0)
		return self._clampParam(val, minv, maxv)

	def _inlinePitchCommandToParam(self, item):
		if hasattr(item, "offset"):
			return self._clampParam(self._pitch + item.offset, self._pitchMin, self._pitchMax)
		multiplier = getattr(item, "multiplier", 1)
		return self._pitchPercentToParam(self._clampPercent(self._pitchPercent * multiplier))

	def _replaceParamAtOffset(self, parameters, offset, paramId, value):
		# Keep the last inline value if duplicate parameters share an offset.
		offset = max(0, offset)
		parameters[:] = [p for p in parameters if not (p[0] == offset and p[2] == paramId)]
		parameters.append((offset, 0, paramId, value))

	def _normaliseLang(self, lang):
		if not lang:
			return None
		return str(lang).replace("_", "-").lower()

	def _countryForVoice(self, voice):
		try:
			return int(self._get_cached_langs()[int(voice)]["country"])
		except (IndexError, KeyError, TypeError, ValueError):
			return DEFAULT_EXCEPTION_LANGUAGE

	def _voiceVariantForLang(self, lang):
		normalised = self._normaliseLang(lang)
		if not normalised:
			return self._voice, self._variant
		langs = self._get_cached_langs()
		primary = normalised.split("-", 1)[0]
		fallbackVoice = None
		for index, langInfo in enumerate(langs):
			country = langInfo.get("country")
			code = self._normaliseLang(LANGUAGES.get(country, langInfo.get("lang")))
			if not code:
				continue
			if code == normalised:
				if index == getattr(self, "_voice", None):
					return self._voice, self._variant
				return index, 0
			if fallbackVoice is None and code.split("-", 1)[0] == primary:
				fallbackVoice = index
		if fallbackVoice is not None:
			return fallbackVoice, 0
		return self._voice, self._variant

	def _set_volume(self, volume):
		# Orpheus doesn't provide a stable API for per-utterance volume.
		# We implement NVDA's Volume setting by scaling the PCM samples.
		self._volume = self._clampPercent(volume)

	def _get_volume(self):
		return getattr(self, "_volume", 100)

	def _set_pitch(self, pitch):
		self._pitchPercent = self._clampPercent(pitch)
		self._pitch = self._pitchPercentToParam(self._pitchPercent)

	def _get_pitch(self):
		return getattr(self, "_pitchPercent", 50)

	def _get_intonation(self):
		return getattr(self, "_intonation", 50)

	def _set_intonation(self, value):
		value = self._clampParam(value, 0, 100)
		if (
			time.time() < getattr(self, "_initialIntonationConfigUntil", 0)
			and value == 50
			and getattr(self, "_intonation", 50) != 50
		):
			return
		self._initialIntonationConfigUntil = 0
		if getattr(self, "_intonation", None) == value:
			return
		self._intonation = value
		self._write_current_voice_intonation(value)
		self._restart_host_preserving_settings()

	def _get_headSize(self):
		return getattr(self, "_headSize", 50)

	def _set_headSize(self, value):
		value = self._clampPercent(value)
		if getattr(self, "_headSize", None) == value:
			return
		self._headSize = value
		self._write_current_voice_head_size(value)
		self._restart_host_preserving_settings()

	def _get_voicing(self):
		return getattr(self, "_voicing", 100)

	def _set_voicing(self, value):
		value = self._clampPercent(value)
		if getattr(self, "_voicing", None) == value:
			return
		self._voicing = value
		self._write_current_voice_voicing(value)
		self._restart_host_preserving_settings()

	def _get_skimReadingLevel(self):
		return getattr(self, "_skimReadingLevel", 0)

	def _set_skimReadingLevel(self, value):
		self._skimReadingLevel = self._clampParam(value, 0, 8)

	def _get_wordPauseAmount(self):
		return getattr(self, "_wordPauseAmount", 0)

	def _set_wordPauseAmount(self, value):
		self._wordPauseAmount = self._coercePauseSetting(value, 1000)

	def _get_phrasePauseAmount(self):
		return getattr(self, "_phrasePauseAmount", 12)

	def _set_phrasePauseAmount(self, value):
		self._phrasePauseAmount = self._coercePauseSetting(value, 2000)

	def cancel(self):
		with self.lock:
			self.is_speaking = False
			self.queue = []
			if self._fallback_done_timer is not None:
				self._fallback_done_timer.cancel()
				self._fallback_done_timer = None
			if self.player is not None:
				self.player.stop()
			try:
				self._send_command("mute", val=3)
			except Exception:
				pass
			# Wake up worker to possibly process next empty state or just reset
			self._work_event.set()

	def _refresh_language_cache(self):
		try:
			langs = self._send_command("getLangs", timeout=METADATA_COMMAND_TIMEOUT)
		except Exception:
			return
		if langs:
			self._langs = langs

	def _get_cached_langs(self):
		return self._langs

	def _get_availableVoices(self):
		langs = self._get_cached_langs()
		infos = {}
		for i, l in enumerate(langs):
			country_code = l['country']
			infos[str(i)] = VoiceInfo(str(i), l['lang'], LANGUAGES.get(country_code, str(country_code)))
		if not infos:
			infos[str(getattr(self, "_voice", 0))] = VoiceInfo(str(getattr(self, "_voice", 0)), "Orpheus", DEFAULT_LANGUAGE)
		return infos

	def _get_voice(self):
		return str(self._voice)

	def _set_voice(self, voice):
		self._voice = int(voice)
		self._refresh_language_cache()
		self._intonation = self._read_current_voice_intonation()
		self._headSize = self._read_current_voice_head_size()
		self._voicing = self._read_current_voice_voicing()

	def _get_availableVariants(self):
		try:
			langs = self._get_cached_langs()
			if not langs:
				self._refresh_language_cache()
				langs = self._get_cached_langs()
			country = langs[self._voice]['country']
			if country not in self._voicesByCountry:
				self._voicesByCountry[country] = self._send_command(
					"getVoices",
					timeout=METADATA_COMMAND_TIMEOUT,
					country=country,
				)
			voices = self._voicesByCountry[country]
		except Exception:
			return {}
		infos = {}
		for i, v in enumerate(voices):
			infos[str(i)] = VoiceInfo(str(i), v['name'])
		return infos

	def _get_variant(self):
		return str(self._variant)

	def _set_variant(self, variant):
		if variant is None:
			self._variant = 0
		else:
			self._variant = int(variant)
		self._apply_current_variant()
		self._refresh_current_voice_params()
		self._intonation = self._read_current_voice_intonation()
		self._headSize = self._read_current_voice_head_size()
		self._voicing = self._read_current_voice_voicing()

	def _apply_current_variant(self):
		if getattr(self, "_process", None) is None:
			return
		# Apply only the variant here. Sending the cached pitch at the same time
		# prevents Orpheus from exposing the selected voice's own pitch default,
		# which makes voices such as Synthetic Andy inherit Synthetic Dave's pitch.
		params = [
			(0, 0, 7, int(getattr(self, "_variant", 0))),
		]
		try:
			self.append(params, ' ')
			self.speak_append()
			self.event.wait(1.0)
			self.event.clear()
		except Exception:
			pass

	def _refresh_current_voice_params(self):
		# Refresh parameter descriptors and update cached pitch bounds. After a
		# voice change, Orpheus reports that voice's default pitch as current.
		self._paramDescs = self.get_params() or []
		self._pitchDesc = self._paramDescs[1] if len(self._paramDescs) > 1 else None
		self._pitchMin = self._pitchDesc.min if self._pitchDesc else FALLBACK_PITCH_MIN
		self._pitchMax = self._pitchDesc.max if self._pitchDesc else FALLBACK_PITCH_MAX
		self._defaultPitch = self._get_default_pitch()
		self._refresh_parameter_ids()
		self._pitch = self._pitchPercentToParam(getattr(self, "_pitchPercent", 50))

	def _refresh_parameter_ids(self):
		self._parameterIds = {}
		for key, tokens, _label, _min_value, _max_value, _default, _min_step, _normal_step, _large_step, _display_name, fallback_id in PARAMETER_SETTINGS:
			match = None
			for index, desc in enumerate(getattr(self, "_paramDescs", ()) or ()):
				name = str(getattr(desc, "name", "") or "").lower()
				if all(token in name for token in tokens):
					match = index
					break
			self._parameterIds[key] = match if match is not None else fallback_id

	def _parameterIdFor(self, key, fallback_id):
		try:
			return int(getattr(self, "_parameterIds", {}).get(key, fallback_id))
		except Exception:
			return fallback_id

	def _restart_host_preserving_settings(self):
		voice = getattr(self, "_voice", 0)
		variant = getattr(self, "_variant", 0)
		pitch_percent = getattr(self, "_pitchPercent", 50)
		rate = getattr(self, "_rate", self._percentToParam(50, 10, 700))
		volume = getattr(self, "_volume", 100)
		parameter_values = {
			key: getattr(self, "_" + key, default)
			for key, _tokens, _label, _min_value, _max_value, default, _min_step, _normal_step, _large_step, _display_name, _fallback_id in PARAMETER_SETTINGS
		}
		with self.lock:
			self.is_speaking = False
			self.queue = []
			if self._fallback_done_timer is not None:
				self._fallback_done_timer.cancel()
				self._fallback_done_timer = None
			try:
				if self.player is not None:
					self.player.stop()
			except Exception:
				pass
		try:
			self._send_command("close", timeout=1.0)
		except Exception:
			pass
		self._stop_reader = True
		self._stop_host_process()
		try:
			self._conn.close()
		except Exception:
			pass
		with self._msg_lock:
			for _msg_id, (evt, _) in self._response_events.items():
				evt.set()
			self._response_events = {}
		self._stop_reader = False
		self._langs = []
		self._voicesByCountry = {}
		self.event.clear()
		self._start_host()
		self._refresh_language_cache()
		self._voice = voice
		self._variant = variant
		self._rate = rate
		self._volume = volume
		for key, value in parameter_values.items():
			setattr(self, "_" + key, value)
		self._pitchPercent = pitch_percent
		self._apply_current_variant()
		self._refresh_current_voice_params()
		self._work_event.set()

	def terminate(self):
		try:
			self.cancel()
		except Exception:
			pass
		try:
			self._send_command("close", timeout=1.0)
		except Exception:
			pass
		self._stop_reader = True
		self._work_event.set()
		self._stop_host_process()
		try:
			self._conn.close()
		except Exception:
			pass
		if self.player is not None:
			self.player.close()
			self.player = None

	def _stop_host_process(self):
		process = getattr(self, "_process", None)
		if process is None:
			return
		if process.poll() is not None:
			self._process = None
			return
		try:
			process.wait(timeout=2.0)
			self._process = None
			return
		except subprocess.TimeoutExpired:
			pass
		try:
			process.terminate()
			process.wait(timeout=2.0)
			self._process = None
			return
		except subprocess.TimeoutExpired:
			pass
		except Exception:
			self._process = None
			return
		try:
			process.kill()
			process.wait(timeout=2.0)
		except Exception:
			pass
		finally:
			self._process = None

	def get_params(self):
		try:
			data = self._send_command("getParams", timeout=METADATA_COMMAND_TIMEOUT)
			return [ParamDesc(**d) for d in data]
		except:
			return []

	def pause(self, switch):
		if self.player is not None:
			self.player.pause(switch)
