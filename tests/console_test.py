"""No-console tests for artifact selection and execution safeguards."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from ftplib import error_perm

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('console_tool', ROOT / 'tools/console.py')
console = importlib.util.module_from_spec(spec)
spec.loader.exec_module(console)

class FakeFTP:
    files = {}
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def connect(self, *args, **kwargs): pass
    def login(self): pass
    def cwd(self, path): pass
    def retrbinary(self, command, write):
        name = command.removeprefix('RETR ')
        if name not in self.files: raise error_perm('550 absent')
        write(self.files[name])
    def storbinary(self, command, stream): self.files[command.removeprefix('STOR ')] = stream.read()

class ConsoleTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'work/tests').mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'work/tests')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'outputs').mkdir()
        for kind, label, version, data in [('usb-diagnostic', 'usb-diag', '0.1.1', b'diagnostic'),
                                            ('usb-bridge', 'usb-bridge', '0.3.2', b'bridge'),
                                            ('usb-probe', 'usb-probe', '0.1.2', b'probe'),
                                            ('bluetooth-probe', 'bluetooth-probe', '0.1.0', b'bluetooth'),
                                            ('bluetooth-bridge', 'bluetooth-bridge', '0.12.0', b'bt-bridge')]:
            (self.root / f'outputs/{kind}-build.json').write_text(json.dumps(
                {'version':version,'sha256':hashlib.sha256(data).hexdigest()}))
            (self.root / f'outputs/dualshock-to-ps4-{label}-{version}.elf').write_bytes(data)
        FakeFTP.files = {}
    def invoke(self, action, kind='diagnostic'):
        args = ['console.py', action, '--host', 'test.invalid', '--run', 'test']
        if kind: args += ['--kind', kind]
        with patch.object(console, 'ROOT', self.root), patch.object(console, 'FTP', FakeFTP), \
             patch('sys.argv', args), contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            console.main()
    def test_diagnostic_stage_preserves_previous_log(self):
        FakeFTP.files['/data/dualshock_to_ps4/usb-diagnostic.log'] = b'previous result'
        self.invoke('stage')
        self.assertEqual(FakeFTP.files['dualshock-to-ps4-usb-diag-0.1.1.elf'], b'diagnostic')
        self.assertEqual((self.root/'work/runs/test/before-usb-diagnostic.log').read_bytes(), b'previous result')
    def test_diagnostic_fetch_uses_own_log_and_final_marker(self):
        FakeFTP.files['/data/dualshock_to_ps4/usb-diagnostic.log'] = b'USB diagnostic 0.1.1\nfinished status=0\n'
        self.invoke('fetch')
        record = json.loads((self.root/'work/runs/test/deployment.json').read_text())
        self.assertTrue(record['execution_confirmed'] and record['finished'])
    def test_diagnostic_cannot_write_bridge_stop_file(self):
        with self.assertRaises(SystemExit) as e: self.invoke('stop')
        self.assertEqual(e.exception.code, 2)
        self.assertEqual(FakeFTP.files, {})
    def test_default_bridge_artifact_is_unchanged(self):
        self.invoke('stage', None)
        self.assertEqual(FakeFTP.files['dualshock-to-ps4-usb-bridge-0.3.2.elf'], b'bridge')
    def test_bluetooth_bridge_own_artifact_log_and_manual_stop(self):
        self.invoke('stage', 'bluetooth-bridge')
        self.assertEqual(FakeFTP.files['dualshock-to-ps4-bluetooth-bridge-0.12.0.elf'], b'bt-bridge')
        FakeFTP.files['/data/dualshock_to_ps4/bluetooth-bridge.log'] = b'Bluetooth bridge 0.12.0\nbridge finished status=0\n'
        self.invoke('fetch', 'bluetooth-bridge')
        record = json.loads((self.root/'work/runs/test/deployment.json').read_text())
        self.assertTrue(record['execution_confirmed'] and record['finished'])
        self.invoke('stop', 'bluetooth-bridge')
        self.assertEqual(FakeFTP.files['/data/dualshock_to_ps4/stop'], b'stop\n')
    def test_duplicate_send_never_opens_network(self):
        self.invoke('stage')
        record = self.root/'work/runs/test/deployment.json'
        data = json.loads(record.read_text()); data['state'] = 'sent'; record.write_text(json.dumps(data))
        with patch.object(console.socket, 'create_connection') as connect:
            with self.assertRaises(SystemExit): self.invoke('send')
            connect.assert_not_called()

    def test_probe_is_separate_from_original_diagnostic(self):
        FakeFTP.files['/data/dualshock_to_ps4/usb-probe.log'] = b'previous probe'
        self.invoke('stage', 'probe')
        self.assertEqual(FakeFTP.files['dualshock-to-ps4-usb-probe-0.1.2.elf'], b'probe')
        self.assertEqual((self.root/'work/runs/test/before-usb-probe.log').read_bytes(), b'previous probe')
        FakeFTP.files['/data/dualshock_to_ps4/usb-probe.log'] = b'USB probe 0.1.2\nfinished status=1\n'
        self.invoke('fetch', 'probe')
        record = json.loads((self.root/'work/runs/test/deployment.json').read_text())
        self.assertTrue(record['execution_confirmed'] and record['finished'])
        with self.assertRaises(SystemExit): self.invoke('stop', 'probe')

    def test_bluetooth_probe_keeps_usb_artifacts_and_logs_separate(self):
        FakeFTP.files['/data/dualshock_to_ps4/bluetooth-probe.log'] = b'previous BT probe'
        self.invoke('stage', 'bluetooth-probe')
        self.assertEqual(FakeFTP.files['dualshock-to-ps4-bluetooth-probe-0.1.0.elf'], b'bluetooth')
        self.assertEqual((self.root/'work/runs/test/before-bluetooth-probe.log').read_bytes(), b'previous BT probe')
        FakeFTP.files['/data/dualshock_to_ps4/bluetooth-probe.log'] = b'Bluetooth probe 0.1.0\nfinished status=0 inspection_only=1\n'
        self.invoke('fetch', 'bluetooth-probe')
        record = json.loads((self.root/'work/runs/test/deployment.json').read_text())
        self.assertTrue(record['execution_confirmed'] and record['finished'])
        self.assertFalse(record.get('hardware_bluetooth_validated', False))
        with self.assertRaises(SystemExit): self.invoke('stop', 'bluetooth-probe')
        self.assertNotIn('/data/dualshock_to_ps4/stop', FakeFTP.files)

    def test_bluetooth_probe_duplicate_send_never_opens_network(self):
        self.invoke('stage', 'bluetooth-probe')
        record = self.root/'work/runs/test/deployment.json'
        data = json.loads(record.read_text()); data['state'] = 'sent'; record.write_text(json.dumps(data))
        with patch.object(console.socket, 'create_connection') as connect:
            with self.assertRaises(SystemExit): self.invoke('send', 'bluetooth-probe')
            connect.assert_not_called()

if __name__ == '__main__': unittest.main()
