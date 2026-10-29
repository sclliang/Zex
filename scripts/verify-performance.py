"""Run the packaged viewer with isolated data; no user preferences are written."""
import argparse,json,os,pathlib,subprocess,time

parser=argparse.ArgumentParser()
parser.add_argument('--label',default='current')
parser.add_argument('--enforce',action='store_true')
parser.add_argument('--package',default='package')
args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parent.parent
package=(root/args.package).resolve()
folder=root/'artifacts'/'performance'/args.label
fixtures=root/'artifacts'/'performance'/'fixtures'
folder.mkdir(parents=True,exist_ok=True);fixtures.mkdir(parents=True,exist_ok=True)
env=os.environ.copy()
system_root=os.environ['SystemRoot']
env['PATH']=system_root+'\\System32;'+system_root
for key in ['QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QT_SCALE_FACTOR','QT_SCREEN_SCALE_FACTORS']:env.pop(key,None)
results=[]
def run(name,files=(),extra=(),scale=None,expect_ready=True):
    report=folder/(name+'.json')
    report.unlink(missing_ok=True)
    command=[str(package/'Zex.exe'),'--verify-package',str(report),'--verify-wait-ms','800',*extra,'--',*[str(p) for p in files]]
    process_env=env.copy()
    if scale is not None:process_env['QT_SCALE_FACTOR']=str(scale)
    started=time.perf_counter()
    process=subprocess.run(command,cwd=fixtures,env=process_env,timeout=45,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW)
    if process.returncode not in (0,1) or not report.exists():raise RuntimeError(name+' failed: '+str(process.returncode))
    data=json.loads(report.read_text(encoding='utf-8'))
    assert data['ready']==expect_ready,name
    data['case']=name;data['process_wall_ms']=round((time.perf_counter()-started)*1000)
    results.append(data)
    print(json.dumps({k:data.get(k) for k in ['case','startup_to_shell_ms','startup_to_ready_ms','max_ui_gap_ms','max_ready_ui_gap_ms','tabs','loaded_tabs','device_pixel_ratio','window_fits','appearance_fits','integration_fits','search_matches']},ensure_ascii=False),flush=True)
    return data

base=run('screen-baseline')
native_ratio=base['device_pixel_ratio']
for ratio in (1,1.25,1.5,2,3,4):
    common=['--capture',str(folder/f'scale-{ratio}.png'),'--capture-appearance',str(folder/f'appearance-{ratio}.png'),'--verify-integration-capture',str(folder/f'integration-{ratio}.png')]
    data=run('scale-'+str(ratio),extra=common,scale=ratio/native_ratio)
    if args.enforce:
        assert all(data[key] for key in ['window_fits','appearance_fits','integration_fits']),data['case']

block=(b'2026-10-30 INFO reading code and text in Zex; payload value 1234567890 abcdef\n'*1024)
for size in (10,100,240):
    path=fixtures/f'large-{size}MiB.log'
    if not path.exists() or path.stat().st_size!=size*1024*1024:
        remaining=size*1024*1024
        with path.open('wb') as stream:
            while remaining:chunk=block[:min(remaining,len(block))];stream.write(chunk);remaining-=len(chunk)
    data=run('large-'+str(size),files=[path],extra=['--find','MISSING_NEEDLE_IN_THIS_DOCUMENT'])
    assert data['search_matches']==0 and '大文件模式' in data['language']
    if args.enforce:assert data['search_complete'] and data['max_ready_ui_gap_ms']<150

longline=fixtures/'long-line.log'
longline.write_bytes(b'x'*(1024*1024)+b' last_line_marker\n')
data=run('long-line',files=[longline],extra=['--find','last_line_marker'])
assert data['search_matches']==1
too_large=fixtures/'over-limit.log'
with too_large.open('wb') as stream:stream.truncate(257*1024*1024)
data=run('over-limit',files=[too_large],expect_ready=False)
assert data['error']

foreground=fixtures/'foreground.txt';foreground.write_text('reading position\n'*300,encoding='utf-8')
tabs=[{'path':str(fixtures/f'background-{i:03}.log').replace('\\','/'),'position':{'topLine':100+i}} for i in range(99)]
tabs.append({'path':foreground.as_posix(),'position':{'topLine':120}})
session=fixtures/'100-tabs.json';session.write_text(json.dumps({'version':1,'tabs':tabs,'activePath':foreground.as_posix()}),encoding='utf-8')
for index in range(3):
    run(f'single-{index}',files=[foreground])
    data=run(f'100-tabs-{index}',extra=['--verify-session',str(session)])
    assert data['tabs']==100 and data['loaded_tabs']==1
    assert [t['path'] for t in data['session']['tabs']]==[t['path'] for t in tabs]
    assert data['session']['activePath']==foreground.as_posix()
(folder/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print('Saved '+str(folder/'summary.json'),flush=True)
