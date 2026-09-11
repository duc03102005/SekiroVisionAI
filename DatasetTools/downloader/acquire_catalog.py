"""Reacquire pinned, permission-reviewed gameplay sources; never extract watch pages.

Development/CI utility only. The Windows application does not require Python.
Usage: python -m DatasetTools.downloader.acquire_catalog --source-id LIVE_YT_GAMING_SEKIRO_005
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import struct
import tempfile
from urllib.parse import urlparse
from urllib.request import Request, build_opener
import zlib

from DatasetTools.common import load_jsonl, safe_id, sha256_file, write_json
from DatasetTools.downloader.ingest import _HttpsRedirects, download_direct, ingest

MAX_MEMBER_BYTES = 1024 ** 3


def pinned_url(url: str) -> str:
    parsed = urlparse(url)
    parts = parsed.path.split('/')
    # Only the author-distributed public Hub files reviewed in this catalog.
    if (parsed.scheme != 'https' or parsed.hostname != 'huggingface.co'
            or parsed.username or parsed.password or parsed.query or parsed.fragment
            or len(parts) < 7 or parts[1] != 'datasets' or parts[4] != 'resolve'
            or len(parts[5]) != 40 or any(c not in '0123456789abcdef' for c in parts[5])):
        raise ValueError('Expected a public HTTPS Hugging Face dataset URL pinned to a commit.')
    return url


def fetch_range(url: str, start: int, length: int, archive_size: int) -> bytes:
    if length <= 0 or length > MAX_MEMBER_BYTES or start < 0 or start + length > archive_size:
        raise ValueError('ZIP range outside declared archive bounds.')
    request = Request(pinned_url(url), headers={'Range': f'bytes={start}-{start + length - 1}'})
    with build_opener(_HttpsRedirects()).open(request, timeout=60) as response:
        expected = f'bytes {start}-{start + length - 1}/{archive_size}'
        if response.status != 206 or response.headers.get('Content-Range') != expected:
            raise ValueError('Server did not honor the exact public-file byte range.')
        data = response.read(length + 1)
    if len(data) != length:
        raise ValueError('Incomplete or oversized ZIP member range.')
    return data


def extract_member(entry: dict, fetch) -> bytes:
    """Decode one named ZIP member, checking its local header, limits and CRC."""
    offset = int(entry['offset'])
    size, compressed_size = int(entry['size']), int(entry['compressed_size'])
    if not 0 < size <= MAX_MEMBER_BYTES or not 0 < compressed_size <= MAX_MEMBER_BYTES:
        raise ValueError('ZIP member exceeds media size limit.')
    header = fetch(offset, 30)
    if len(header) != 30:
        raise ValueError('Truncated ZIP member header.')
    signature, _, flags, method, _, _, _, _, _, name_size, extra_size = struct.unpack('<4s5H3L2H', header)
    if signature != b'PK\x03\x04' or flags & 1 or method not in (0, 8) or method != entry['compress_type']:
        raise ValueError('Unsupported, encrypted, or mismatched ZIP entry.')
    if not 0 < name_size <= 1024 or extra_size > 65535:
        raise ValueError('Invalid ZIP entry metadata size.')
    name = fetch(offset + 30, name_size).decode('utf-8' if flags & 0x800 else 'cp437')
    if name != entry['name'] or Path(name).name.startswith('.') or '..' in Path(name).parts:
        raise ValueError('ZIP member identity/path mismatch.')
    packed = fetch(offset + 30 + name_size + extra_size, compressed_size)
    if len(packed) != compressed_size:
        raise ValueError('Truncated compressed ZIP entry.')
    if method == 8:
        decoder = zlib.decompressobj(-15)
        data = decoder.decompress(packed, size + 1)
        if not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
            raise ValueError('Invalid or oversized deflated media.')
    else:
        data = packed
    if len(data) != size or zlib.crc32(data) != int(entry['crc32'], 16):
        raise ValueError('ZIP member CRC32/size mismatch.')
    return data


def save_provenance(directory: Path, row: dict, evidence: Path) -> None:
    shutil.copy2(evidence, directory / evidence.name)
    write_json(directory / 'catalog-provenance.json', row)
    # Source exclusions are authoritative for training/mining. They never alter
    # original source bytes or timestamps, and are not attack annotations.
    write_json(directory / 'training-exclusions.json', {
        'source_id': row['source_id'], 'intervals_ms': row.get('training_exclusions_ms', []),
        'candidate_intervals_ms': row.get('training_candidate_intervals_ms', []),
        'selection_note': row.get('selection_note', ''),
        'review_status': row.get('annotation_status', 'UNREVIEWED')})


def acquire_one(row: dict, root: Path, repo_root: Path, max_bytes: int) -> dict:
    source_id = safe_id(row['source_id'])
    if row.get('example_only') or row.get('usage_basis') not in ('OPEN_LICENSE', 'EXPLICIT_PERMISSION'):
        raise ValueError('Only real permission-reviewed catalog sources may be acquired.')
    digest = row.get('sha256', '')
    if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Catalog must pin the extracted media SHA256.')
    evidence_name = row.get('license_notice') or row.get('permission_evidence')
    if not evidence_name:
        raise ValueError('Catalog source requires a bundled license/permission record.')
    evidence = (repo_root / evidence_name).resolve()
    if not evidence.is_relative_to(repo_root.resolve()) or not evidence.is_file():
        raise ValueError('Missing or out-of-repository permission evidence.')
    existing = [r for r in load_jsonl(root / 'source_manifest.jsonl') if r.get('source_id') == source_id]
    if existing:
        if len(existing) != 1:
            raise ValueError('Duplicate immutable source ID in the local manifest.')
        current = existing[0]
        if current.get('sha256') != digest or not Path(current['media_path']).is_file() or sha256_file(Path(current['media_path'])) != digest:
            raise ValueError('Existing source identity or local bytes do not match catalog.')
        save_provenance(Path(current['media_path']).parent, row, evidence)
        return {'source_id': source_id, 'status': 'VERIFIED_EXISTING', 'sha256': digest}
    acquisition = row['acquisition']
    with tempfile.TemporaryDirectory(prefix='svai-approved-source-') as folder:
        media = Path(folder) / 'source.mp4'
        if 'zip_entry' in acquisition:
            entry = acquisition['zip_entry']
            if int(entry['size']) > max_bytes:
                raise ValueError('Selected media exceeds download budget.')
            url = acquisition.get('download_url') or row['url'].split('#', 1)[0]
            pinned_url(url)
            raw = extract_member(entry, lambda start, size: fetch_range(url, start, size, acquisition['archive_size_bytes']))
            media.write_bytes(raw)
        else:
            url = pinned_url(acquisition['download_url'])
            if int(acquisition['media_size_bytes']) > max_bytes:
                raise ValueError('Selected media exceeds download budget.')
            download_direct(url, media, max_bytes)
            if media.stat().st_size != int(acquisition['media_size_bytes']):
                raise ValueError('Downloaded source size does not match publisher manifest.')
        if sha256_file(media) != digest:
            raise ValueError('Downloaded media SHA256 does not match the reviewed source.')
        record = ingest(input_path=str(media), source_id=source_id, creator=row['creator'],
                        session_id=row['session_id'], boss=row['boss'],
                        usage_basis=row['usage_basis'], usage_evidence=row['usage_evidence'],
                        source_url=row['url'], license_note=row.get('license_or_usage_note', evidence_name),
                        duplicate_group_id=row.get('duplicate_group_id', ''), root=root, max_bytes=max_bytes)
    save_provenance(Path(record['media_path']).parent, row, evidence)
    return {'source_id': source_id, 'status': 'ACQUIRED_SHA256_VERIFIED',
            'sha256': digest, 'frames': record['video']['frame_count'],
            'training_exclusions_ms': row.get('training_exclusions_ms', [])}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, default=Path('Dataset/catalog/acquired_sources.json'))
    parser.add_argument('--root', type=Path, default=Path('data'))
    parser.add_argument('--source-id', action='append', default=[], help='Exact ID; repeat for several sources. Omit to acquire all catalog sources.')
    parser.add_argument('--max-source-bytes', type=int, default=MAX_MEMBER_BYTES)
    args = parser.parse_args()
    catalog = json.loads(args.catalog.read_text(encoding='utf-8'))
    rows = catalog['sources']
    wanted = set(args.source_id)
    unknown = wanted - {r['source_id'] for r in rows}
    if unknown:
        parser.error('Unknown source IDs: ' + ', '.join(sorted(unknown)))
    repo_root = Path(__file__).resolve().parents[2]
    for row in rows:
        if not wanted or row['source_id'] in wanted:
            print(json.dumps(acquire_one(row, args.root.resolve(), repo_root, args.max_source_bytes)), flush=True)


if __name__ == '__main__':
    main()
