import os
import time

from calibre.devices.errors import ControlError
from calibre.devices.interface import DevicePlugin
from calibre.devices.usbms.deviceconfig import DeviceConfig

from . import ws_client
from .config import CrossPointConfigWidget, PREFS
from .log import add_log


class CrossPointDevice(DeviceConfig, DevicePlugin):
    name = 'CrossPoint Reader'
    gui_name = 'CrossPoint Reader'
    description = 'CrossPoint Reader wireless device'
    supported_platforms = ['windows', 'osx', 'linux']
    author = 'CrossPoint Reader'
    version = (0, 1, 0)

    # Invalid USB vendor info to avoid USB scans matching.
    VENDOR_ID = [0xFFFF]
    PRODUCT_ID = [0xFFFF]
    BCD = [0xFFFF]

    FORMATS = ['epub']
    ALL_FORMATS = ['epub']
    SUPPORTS_SUB_DIRS = True
    MUST_READ_METADATA = False
    MANAGES_DEVICE_PRESENCE = True
    DEVICE_PLUGBOARD_NAME = 'CROSSPOINT_READER'

    def __init__(self, path):
        super().__init__(path)
        self.is_connected = False
        self.device_host = None
        self.device_port = None
        self.last_discovery = 0.0
        self.report_progress = lambda x, y: x
        self._debug_enabled = False

    def _log(self, message):
        add_log(message)
        if self._debug_enabled:
            try:
                self.report_progress(0.0, message)
            except Exception:
                pass

    # Device discovery / presence
    def _discover(self):
        now = time.time()
        if now - self.last_discovery < 2.0:
            return None, None
        self.last_discovery = now
        host, port = ws_client.discover_device(
            timeout=1.0,
            debug=PREFS['debug'],
            logger=self._log,
            extra_hosts=[PREFS['host']],
        )
        if host and port:
            return host, port
        return None, None

    def detect_managed_devices(self, devices_on_system, force_refresh=False):
        if self.is_connected:
            return self
        debug = PREFS['debug']
        self._debug_enabled = debug
        if debug:
            self._log('[CrossPoint] detect_managed_devices')
        host, port = self._discover()
        if host:
            if debug:
                self._log(f'[CrossPoint] discovered {host} {port}')
            self.device_host = host
            self.device_port = port
            self.is_connected = True
            return self
        if debug:
            self._log('[CrossPoint] discovery failed')
        return None

    def open(self, connected_device, library_uuid):
        if not self.is_connected:
            raise ControlError(desc='Attempt to open a closed device')
        return True

    def get_device_information(self, end_session=True):
        host = self.device_host or PREFS['host']
        device_info = {
            'device_store_uuid': 'crosspoint-' + host.replace('.', '-'),
            'device_name': 'CrossPoint Reader',
            'device_version': '1',
        }
        return (self.gui_name, '1', '1', '', {'main': device_info})

    def reset(self, key='-1', log_packets=False, report_progress=None, detected_device=None):
        self.set_progress_reporter(report_progress)

    def set_progress_reporter(self, report_progress):
        if report_progress is None:
            self.report_progress = lambda x, y: x
        else:
            self.report_progress = report_progress

    def config_widget(self):
        return CrossPointConfigWidget()

    def save_settings(self, config_widget):
        config_widget.save()

    def books(self, oncard=None, end_session=True):
        # Device does not expose a browsable library yet.
        return []

    def sync_booklists(self, booklists, end_session=True):
        # No on-device metadata sync supported.
        return None

    def card_prefix(self, end_session=True):
        return None, None

    def total_space(self, end_session=True):
        return 10 * 1024 * 1024 * 1024, 0, 0

    def free_space(self, end_session=True):
        return 10 * 1024 * 1024 * 1024, 0, 0

    def upload_books(self, files, names, on_card=None, end_session=True, metadata=None):
        host = self.device_host or PREFS['host']
        port = self.device_port or PREFS['port']
        upload_path = PREFS['path']
        chunk_size = PREFS['chunk_size']
        if chunk_size > 2048:
            self._log(f'[CrossPoint] chunk_size capped to 2048 (was {chunk_size})')
            chunk_size = 2048
        debug = PREFS['debug']

        paths = []
        total = len(files)
        for i, (infile, name) in enumerate(zip(files, names)):
            if hasattr(infile, 'read'):
                filepath = getattr(infile, 'name', None)
                if not filepath:
                    raise ControlError(desc='In-memory uploads are not supported')
            else:
                filepath = infile
            filename = os.path.basename(name)

            def _progress(sent, size):
                if size > 0:
                    self.report_progress((i + sent / float(size)) / float(total),
                                         'Transferring books to device...')

            ws_client.upload_file(
                host,
                port,
                upload_path,
                filename,
                filepath,
                chunk_size=chunk_size,
                debug=debug,
                progress_cb=_progress,
                logger=self._log,
            )
            paths.append((filename, os.path.getsize(filepath)))

        self.report_progress(1.0, 'Transferring books to device...')
        return paths

    def add_books_to_metadata(self, locations, metadata, booklists):
        # No on-device catalog to update yet.
        return

    def delete_books(self, paths, end_session=True):
        # Deletion not supported in current device API.
        raise ControlError(desc='Device does not support deleting books')

    def eject(self):
        self.is_connected = False

    def is_dynamically_controllable(self):
        return 'crosspoint'

    def start_plugin(self):
        return None

    def stop_plugin(self):
        self.is_connected = False
