"""Acquisition checks use local ZIP bytes, never internet or gameplay fixtures."""
import io
import struct
import unittest
import zipfile

from DatasetTools.downloader.acquire_catalog import extract_member, pinned_url


class CatalogAcquisitionTests(unittest.TestCase):
    def fixture(self, method):
        stream = io.BytesIO()
        data = bytes(range(256)) * 40
        with zipfile.ZipFile(stream, 'w', compression=method) as archive:
            archive.writestr('video/Sekiro-test.mp4', data)
        raw = stream.getvalue()
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            info = archive.infolist()[0]
        entry = dict(offset=info.header_offset, name=info.filename, size=info.file_size,
                     compressed_size=info.compress_size, compress_type=info.compress_type,
                     crc32=f'{info.CRC:08x}')
        return raw, data, entry

    def test_stored_and_deflated_members_verify(self):
        for method in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            raw, expected, entry = self.fixture(method)
            self.assertEqual(expected, extract_member(entry, lambda start, length: raw[start:start + length]))

    def test_wrong_identity_crc_and_decompressed_size_rejected(self):
        raw, _, entry = self.fixture(zipfile.ZIP_DEFLATED)
        for change in ({'name': 'video/other.mp4'}, {'crc32': '00000000'}, {'size': 1}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                extract_member(entry | change, lambda start, length: raw[start:start + length])

    def test_encrypted_and_truncated_member_rejected(self):
        raw, _, entry = self.fixture(zipfile.ZIP_STORED)
        encrypted = bytearray(raw)
        struct.pack_into('<H', encrypted, 6, 1)
        with self.assertRaises(ValueError):
            extract_member(entry, lambda start, length: encrypted[start:start + length])
        with self.assertRaises(ValueError):
            extract_member(entry, lambda start, length: raw[start:start + length - 1])

    def test_only_pinned_public_dataset_urls(self):
        base = 'https://huggingface.co/datasets/owner/dataset/resolve/'
        self.assertEqual(base + 'a' * 40 + '/video.zip', pinned_url(base + 'a' * 40 + '/video.zip'))
        for url in (base + 'main/video.zip', 'http://huggingface.co/datasets/o/d/resolve/' + 'a' * 40 + '/a.mp4',
                    'https://youtube.com/watch?v=example', base + 'a' * 40 + '/a.mp4?token=secret',
                    'https://user:password@huggingface.co/datasets/o/d/resolve/' + 'a' * 40 + '/a.mp4'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                pinned_url(url)


if __name__ == '__main__':
    unittest.main()
