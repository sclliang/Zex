"""Portable search/bookmark/split checks. Uses only the standard library and isolated sessions."""
import argparse, hashlib, json, os, pathlib, subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--label', default='release-0.7.0')
parser.add_argument('--package', default='package')
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parent.parent
package = (root / args.package).resolve()
folder = root / 'artifacts' / 'reading' / args.label
fixtures = folder / 'fixtures'
fixtures.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env['PATH'] = os.environ['SystemRoot'] + '\\System32;' + os.environ['SystemRoot']
for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_SCALE_FACTOR', 'QT_SCREEN_SCALE_FACTORS'):
    env.pop(key, None)
results = []

def run(name, extra=(), files=(), scale=None, wait=600):
    report = folder / (name + '.json')
    report.unlink(missing_ok=True)
    current_env = env.copy()
    if scale is not None:
        current_env['QT_SCALE_FACTOR'] = str(scale)
    command = [str(package / 'Zex.exe'), '--verify-package', str(report), '--verify-wait-ms', str(wait), *extra, '--', *map(str, files)]
    process = subprocess.run(command, cwd=fixtures, env=current_env, capture_output=True, timeout=35, creationflags=subprocess.CREATE_NO_WINDOW)
    assert process.returncode == 0 and report.exists(), (name, process.returncode, process.stderr.decode(errors='replace'))
    data = json.loads(report.read_text(encoding='utf-8'))
    assert data['version'] == '0.7.0.0' and data['ready'] and data.get('read_only'), name
    data['case'] = name
    results.append(data)
    print(json.dumps({key: data.get(key) for key in ('case', 'loaded_tabs', 'startup_to_ready_ms', 'max_ready_ui_gap_ms', 'search_matches', 'search_error', 'search_stopped', 'search_controls_fit')}, ensure_ascii=False), flush=True)
    return data

sample = fixtures / '中文 日志.log'
sample.write_bytes(('2026-10-30 INFO 中文 123\r\n2026-10-30 WARN 项目 456\r\n' * 300).encode('utf-8'))
sample_hash = hashlib.sha256(sample.read_bytes()).hexdigest()
names = [sample] + [fixtures / f'dormant-{i:03}.txt' for i in range(99)]
session = fixtures / 'split-session.json'
left_position = dict(topLine=40, caretLine=45, caretColumn=3, anchorLine=45, anchorColumn=3)
right_position = dict(topLine=210, caretLine=220, caretColumn=7, anchorLine=220, anchorColumn=7)
seed = dict(version=1, activePath=sample.as_posix(), tabs=[dict(path=p.as_posix(), position=left_position if i == 0 else {}, bookmarks=[3, 22, 300], pinned=i < 2) for i, p in enumerate(names)], split=dict(enabled=True, activeRight=True, leftPath=sample.as_posix(), rightPath=sample.as_posix(), rightPosition=right_position, rightEncoding='utf-8', leftPercent=55))
def reset_session():
    session.write_text(json.dumps(seed, ensure_ascii=False), encoding='utf-8')

reset_session()
first = run('split-restore', ['--verify-session', str(session)])
assert first['tabs'] == 100 and first['loaded_tabs'] == 2
assert first['session']['tabs'][0]['position']['topLine'] == 40
assert first['session']['split']['rightPosition']['topLine'] == 210
assert first['session']['split']['rightEncoding'] == 'utf-8'
second = run('split-restart', ['--verify-session', str(session)])
assert second['session'] == first['session']
marked = run('bookmarks-save', ['--verify-session', str(session), '--verify-bookmarks', '1,44,599'])
assert marked['session']['tabs'][0]['bookmarks'] == [0, 43, 598]
again = run('bookmarks-restart', ['--verify-session', str(session)])
assert again['session'] == marked['session']
ratio = first['device_pixel_ratio']
for theme in ('graphite', 'midnight', 'north', 'violet', 'paper', 'sand'):
    reset_session()
    name = 'reading-' + theme
    data = run(name, ['--verify-session', str(session), '--verify-theme', theme, '--find', r'WARN.*\d+', '--verify-regex', '--verify-results', '--capture', str(folder / (name + '.png'))])
    assert data['search_matches'] == 300 and data['search_results_rows'] == 300 and not data['search_error']
    assert data['search_controls_fit'] and data['screenshot_saved'] and data['loaded_tabs'] == 2
    assert data['document_sha256'] == sample_hash
for scale in (1, 1.25, 1.5, 2, 3, 4):
    reset_session()
    name = 'reading-scale-' + str(scale)
    data = run(name, ['--verify-session', str(session), '--find', '中文', '--verify-results', '--capture', str(folder / (name + '.png'))], scale=scale / ratio)
    assert data['search_matches'] == 300 and data['search_controls_fit'] and data['screenshot_saved'] and data['window_fits']

small = fixtures / 'selection.txt'
small.write_bytes(b'alpha 123\nalpha 456\nlast 789\n')
selection = run('selection-regex', ['--find', r'\d+', '--verify-regex', '--verify-scope', '0:20', '--verify-results'], [small])
assert selection['search_matches'] == 2 and selection['search_results_rows'] == 2
invalid = run('invalid-expression', ['--find', '(', '--verify-regex'], [small])
assert invalid['search_error'] and invalid['search_complete'] and invalid['max_ready_ui_gap_ms'] < 200
pathological = fixtures / 'pathological.txt'
pathological.write_bytes(b'a' * 100000 + b'!')
bounded = run('bounded-backtracking', ['--find', '(a+)+$', '--verify-regex'], [pathological])
assert bounded['search_error'] and bounded['max_ready_ui_gap_ms'] < 200
large = fixtures / 'cancel.log'
large.write_bytes(b'stream entry without digits\n' * 2500000)
canceled = run('cancel-regex', ['--find', r'\d+', '--verify-regex', '--verify-search-cancel-ms', '80'], [large], wait=1000)
assert canceled['search_stopped'] and canceled['search_complete'] and canceled['max_ready_ui_gap_ms'] < 200
assert canceled['document_sha256'] == hashlib.sha256(large.read_bytes()).hexdigest()
assert hashlib.sha256(sample.read_bytes()).hexdigest() == sample_hash
(folder / 'summary.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
print('Saved ' + str(folder / 'summary.json'), flush=True)
