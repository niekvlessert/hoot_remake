import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / "tools" / "hootcatalog" / "hoot_master_catalog.py"
spec = importlib.util.spec_from_file_location("hoot_master_catalog_tested", MODULE)
master = importlib.util.module_from_spec(spec)
assert spec and spec.loader
import sys
sys.modules[spec.name] = master
spec.loader.exec_module(master)


def make_manifest(root: Path, name: str, games):
    d = root / name
    (d / "shards").mkdir(parents=True)
    shard = {"format": "hoot-catalog-shard", "version": 1, "source_file": f"{name}.xml", "games": games}
    (d / "shards" / "000.json").write_text(json.dumps(shard), encoding="utf-8")
    manifest = {"format": "hoot-catalog-manifest", "version": 1, "source": name, "bindings": [], "includes": ["shards/000.json"]}
    p = d / "hoot.catalog.json"
    p.write_text(json.dumps(manifest), encoding="utf-8")
    return p


def game(gid, title, archive, driver="x68k/generic", dtype="generic", asset="old.bin"):
    return {
        "id": gid, "title": title, "archive": archive,
        "driver": {"name": driver, "type": dtype, "alias": "", "platform": "TEST"},
        "options": [], "assets": [{"type":"code","path":asset,"transform":"","offset":"0"}],
        "title_entries": [{"kind":"title","code":"1","title":"Track"}],
        "default_sample_rate": 44100, "refresh_hz": 60, "source_file": "test.xml", "source_order": 0,
    }


class MasterCatalogTests(unittest.TestCase):
    def test_bare_hexadecimal_track_codes_are_accepted(self):
        self.assertEqual(0x289A, master.core.parse_num("289a"))

    def test_cp932_community_xml_labelled_shift_jis_is_imported(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            xml = root / "systems.xml"
            document = """<?xml version=\"1.0\" encoding=\"Shift_JIS\"?>
<gamelist><game><name>髙橋サウンド</name><driver type=\"hornet\">konami</driver>
<romlist archive=\"sample\"><rom type=\"code\">code.bin</rom></romlist>
<titlelist><title code=\"1\">日本語</title></titlelist></game></gamelist>"""
            xml.write_bytes(document.encode("cp932"))
            manifest = master.core.import_xml(xml, root / "json")
            games = list(master.core.iter_json_games(manifest))
            self.assertEqual("髙橋サウンド", games[0]["title"])
            self.assertEqual("日本語", games[0]["title_entries"][0]["title"])

    def test_priority_replaces_exact_variant_but_preserves_selected_source_id(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            low=make_manifest(root,"low",[game("old-id","Same","samepack",asset="old.bin")])
            high=make_manifest(root,"high",[game("new-id","Same","samepack",asset="new.bin")])
            out=root/"out"
            merged=master.merge_sources([
                master.SourceSpec("low",100,low), master.SourceSpec("high",200,high)
            ],out)
            games=list(master.core.iter_json_games(merged))
            self.assertEqual(1,len(games))
            self.assertEqual("new-id",games[0]["id"])
            self.assertEqual("new.bin",games[0]["assets"][0]["path"])
            self.assertEqual("high",games[0]["selected_source"])
            self.assertEqual(["high","low"],[p["source"] for p in games[0]["catalog_provenance"]])

    def test_existing_ids_are_not_reassigned_when_only_baseline_is_loaded(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            base=make_manifest(root,"base",[
                game("pack-beep","B variant","pack",driver="pc98dos",dtype="beep"),
                game("pack-beep-2","A variant","pack",driver="pc98dos",dtype="beep"),
            ])
            merged=master.merge_sources([master.SourceSpec("base",200,base)],root/"out")
            self.assertEqual({"pack-beep","pack-beep-2"},{g["id"] for g in master.core.iter_json_games(merged)})

    def test_same_archive_different_variant_is_not_collapsed(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td)
            a=make_manifest(root,"a",[game("opn","Game (OPN)","pack",driver="pc98dos",dtype="opn")])
            b=make_manifest(root,"b",[game("opna","Game (OPNA)","pack",driver="pc98dos",dtype="opna")])
            merged=master.merge_sources([master.SourceSpec("a",100,a),master.SourceSpec("b",200,b)],root/"out")
            games=list(master.core.iter_json_games(merged))
            self.assertEqual(2,len(games))
            self.assertEqual({"opn","opna"},{g["id"] for g in games})


if __name__ == "__main__":
    unittest.main()
