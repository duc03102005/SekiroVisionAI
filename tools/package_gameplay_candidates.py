"""Preserve real trained development candidates without promoting them to production."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def digest(path):
    result = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('phase_root', type=Path)
    parser.add_argument('target_root', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('sources', type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    evidence = json.loads((args.phase_root/'run-evidence.json').read_text())
    target = json.loads((args.target_root/'report.json').read_text())
    if evidence['auto_dodge_eligible'] or not target['trained_on_actual_gameplay']:
        raise ValueError('Expected actual, explicitly non-production candidate evidence')
    args.output.mkdir(parents=True, exist_ok=False)
    phase = args.phase_root/'phase-model'
    shutil.copy2(phase/'model.onnx', args.output/'model.onnx')
    shutil.copy2(phase/'checkpoint.pt', args.output/'attack-phase-checkpoint.pt')
    shutil.copy2(args.target_root/'targets.onnx', args.output/'targets.onnx')
    shutil.copy2(args.target_root/'targets.pt', args.output/'target-checkpoint.pt')
    shutil.copy2(args.phase_root/'motion-representation/checkpoint.pt', args.output/'motion-checkpoint.pt')
    for source, name in [(args.phase_root/'run-evidence.json', 'phase-training-evidence.json'),
                         (args.target_root/'report.json', 'target-training-evidence.json'),
                         (phase/'config.json', 'phase-config.json'),
                         (repo/'Dataset/catalog/licenses/LIVE-YT-Gaming.txt', 'LIVE-YT-Gaming-LICENSE.txt')]:
        shutil.copy2(source, args.output/name)
    rows = [json.loads(line) for line in args.sources.read_text().splitlines() if line.strip()]
    source = next(row for row in rows if row['source_id'] == 'LIVE_YT_GAMING_SEKIRO_005')
    if source['usage_basis'] != 'OPEN_LICENSE' or digest(source['media_path']) != source['sha256']:
        raise ValueError('Licensed real excerpt source does not match immutable provenance')
    clip = args.output/'real-sekiro-excerpt.mp4'
    subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-i', source['media_path'], '-t', '3',
                    '-an', '-vf', 'scale=960:-2', '-c:v', 'libx264', '-crf', '18',
                    '-fps_mode', 'passthrough', '-movflags', '+faststart', str(clip)],
                   check=True, timeout=90)
    files = {path.name: digest(path) for path in args.output.iterdir() if path.is_file()}
    manifest = {
        'status': 'DEVELOPMENT_CANDIDATES_NOT_A_FUNCTIONAL_AUTO_DODGE_RELEASE',
        'actual_gameplay_training': True, 'auto_dodge_eligible': False,
        'observed_contact_tti_labels': 0, 'independent_validation_groups': 0,
        'source_sha256': source['sha256'], 'source_url': source['url'],
        'excerpt': {'source': source['source_id'], 'start_seconds': 0, 'duration_seconds': 3,
                    'transform': '960-pixel-wide H264 CRF18, source cadence retained, audio removed'},
        'files_sha256': files,
        'limitations': ['No supported threat/TTI heads', 'Not all-boss coverage',
                        'AI visual review is not independent human ground truth',
                        'No RTX 3070 live-game measurement'],
    }
    (args.output/'candidate-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps({'files': len(files), 'status': manifest['status']}))


if __name__ == '__main__':
    main()
