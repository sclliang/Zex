"""Exercise navigation and log following from the portable package, with isolated files."""
import argparse, hashlib, json, os, pathlib, subprocess, time

parser = argparse.ArgumentParser()
parser.add_argument('--label', default='release-0.7.0')
parser.add_argument('--package', default='package')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
package = (root / args.package).resolve()
folder = root / 'artifacts' / 'navigation' / args.label
fixtures = folder / 'fixtures'
fixtures.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot']
for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS'):
    env.pop(key, None)
results = []

def launch(name, extra, files=(), wait=800, scale=None):
    report = folder / (name + '.json')
    report.unlink(missing_ok=True)
    process_env = env.copy()
    if scale is not None:
        process_env['QT_SCALE_FACTOR'] = str(scale)
    command = [str(package / 'Zex.exe'), '--verify-package', str(report), '--verify-wait-ms', str(wait), *extra, '--', *map(str, files)]
    process = subprocess.Popen(command, cwd=fixtures, env=process_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
    return process, report

def finish(name, process, report):
    out, err = process.communicate(timeout=35)
    assert process.returncode == 0 and report.exists(), (name, process.returncode, err.decode(errors='replace'))
    data = json.loads(report.read_text(encoding='utf-8'))
    assert data['version'] == '0.7.0.0' and data['ready'], name
    data['case'] = name
    results.append(data)
    print(json.dumps({key: data.get(key) for key in ('case', 'tabs', 'loaded_tabs', 'startup_to_ready_ms', 'max_ready_ui_gap_ms', 'picker_matches', 'picker_fits', 'following', 'follow_notice')}, ensure_ascii=False), flush=True)
    return data

active = fixtures / 'application.log'
active.write_text('2026-10-30 INFO Zex navigation and log following\n' * 1000, encoding='utf-8')
paths = [fixtures / 'client' / 'main.cpp', fixtures / 'server' / 'main.cpp', fixtures / 'README.md']
paths += [fixtures / 'background' / f'file-{i:03}.txt' for i in range(96)]
paths += [active]
session = fixtures / 'navigation-session.json'
session.write_text(json.dumps({'version': 1, 'activePath': active.as_posix(), 'tabs': [{'path': p.as_posix()} for p in paths]}), encoding='utf-8')
for theme in ('graphite', 'midnight', 'north', 'violet', 'paper', 'sand'):
    name = 'picker-' + theme
    process, report = launch(name, ['--verify-session', str(session), '--verify-pin', '0,1', '--verify-theme', theme, '--capture', str(folder / (name + '-window.png')), '--capture-picker', str(folder / (name + '.png')), '--verify-picker-query', 'main'])
    data = finish(name, process, report)
    assert data['theme'] == theme
    assert data['tabs'] == 100 and data['loaded_tabs'] == 1
    assert data['picker_matches'] == 2 and data['picker_fits'] and data['picker_saved']
    assert [t['path'] for t in data['session']['tabs']] == [p.as_posix() for p in paths]
    assert all(t['pinned'] for t in data['session']['tabs'][:2])

ratio = results[0]['device_pixel_ratio']
for scale in (1, 1.25, 1.5, 2, 3, 4):
    name = 'all-tabs-' + str(scale)
    process, report = launch(name, ['--verify-session', str(session), '--verify-all-tabs', '--capture-picker', str(folder / (name + '.png'))], scale=scale / ratio)
    data = finish(name, process, report)
    assert data['picker_matches'] == 100 and data['picker_fits'] and data['picker_saved']
    assert data['loaded_tabs'] == 1 and all(t['pinned'] for t in data['session']['tabs'][:2])

def following(name, initial, actions, expected, encoding='utf-8', stopped=False):
    path = fixtures / (name + '.log')
    path.write_bytes(initial)
    marker = folder / (name + '.ready')
    marker.unlink(missing_ok=True)
    process, report = launch(name, ['--verify-follow', '--verify-encoding', encoding, '--verify-follow-ready', str(marker), '--capture', str(folder / (name + '.png'))], files=[path], wait=5000)
    try:
        deadline = time.monotonic() + 15
        while not marker.exists():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(name + ': follow initialization did not finish')
            time.sleep(.02)
        for action, payload in actions:
            if action == 'append':
                with path.open('ab') as stream:
                    stream.write(payload)
            elif action == 'replace':
                replacement = path.with_suffix('.new')
                replacement.write_bytes(payload)
                os.replace(replacement, path)
            elif action == 'truncate':
                path.write_bytes(payload)
            elif action == 'remove':
                path.unlink()
            else:
                raise AssertionError(action)
            time.sleep(.65)
        source_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        data = finish(name, process, report)
        assert data['document_sha256'] == hashlib.sha256(expected).hexdigest(), name
        assert data['following'] != stopped and not data['follow_paused'], name
        assert data['max_ready_ui_gap_ms'] < 200, name
        assert hashlib.sha256(path.read_bytes()).hexdigest() == source_hash, name + ': source was modified'
        if stopped:
            assert data['follow_notice'], name
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()

initial = b'2026-10-30 INFO streaming log entry\n' * 30000
extra = ('新增🙂 日志\r\n' * 180000).encode('utf-8')
following('follow-burst', initial, [('append', extra[:-2]), ('append', extra[-2:])], initial + extra)
for codec in ('utf-16le', 'gb18030'):
    initial = ('header\r\n' * 1000).encode(codec)
    extra = '中文🙂 新日志\r\n'.encode(codec)
    following('follow-' + codec, initial, [('append', extra[:5]), ('append', extra[5:])], ('header\r\n' * 1000 + '中文🙂 新日志\r\n').encode('utf-8'), codec)
following('follow-rotation', b'old log\n' * 1000, [('replace', b'rotated\n' * 200), ('truncate', b'truncated\n'), ('remove', b''), ('truncate', b'returned\n')], b'returned\n')
following('follow-invalid', b'valid original\n', [('append', b'\xff')], b'valid original\n', stopped=True)
(folder / 'summary.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
print('Saved ' + str(folder / 'summary.json'), flush=True)
