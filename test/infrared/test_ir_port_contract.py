from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/app/app_09/infrared.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/app/app_09/infrared.h").read_text(encoding="utf-8")
HP_UI = (ROOT / "src/app/app_common/hp_ui.h").read_text(encoding="utf-8")
MK_TUI = (ROOT / "src/app/app_common/mk_tui.h").read_text(encoding="utf-8")


class InfraredPortContractTest(unittest.TestCase):
    def test_pr45_saved_remote_ui_is_present(self):
        self.assertIn('"<-", "OK"', SOURCE)
        self.assertIn("LearnSaveLocation", HEADER)
        self.assertIn("LearnSaveFolderPicker", HEADER)
        self.assertIn("LearnSaveFolderName", HEADER)
        self.assertIn("LearnSaveExistingConfirm", HEADER)
        self.assertIn("DeleteConfirm", HEADER)
        self.assertIn("_remoteDir", HEADER)
        self.assertIn('"SAVE LOCATION"', SOURCE)
        self.assertIn('"CHOOSE FOLDER"', SOURCE)
        self.assertIn('"NEW FOLDER"', SOURCE)

    def test_saved_remotes_hides_reserved_universal_folder(self):
        self.assertIn(
            'if (includeFolders && base.length() > 0 &&\n'
            '                    !(strcmp(dir, IR_DIR) == 0 && strcasecmp(base.c_str(), "universal") == 0))',
            SOURCE,
        )
        self.assertIn("_listIrFiles(UNIV_DIR, _fileList);", SOURCE)

    def test_universal_remote_rendering_is_unchanged(self):
        self.assertIn('hp::drawHeader(Lcd, _univCatTitle, "REMOTE", hp::COL_FG);', SOURCE)
        self.assertIn(
            'hp::drawFooter3(Lcd, "[^v<>]Select", "[A]Blast", "[B]Back");',
            SOURCE,
        )

    def test_shared_direct_draw_palettes_use_feralcat_red(self):
        self.assertIn("static constexpr uint16_t COL_FG       = 0xC800;", HP_UI)
        self.assertIn("static constexpr uint16_t COL_HL       = 0xC800;", HP_UI)
        self.assertIn("static constexpr uint32_t ACCENT      = 0xFF2A3D;", MK_TUI)
        self.assertIn("static constexpr uint32_t SEL_BG      = 0xFF2A3D;", MK_TUI)
        self.assertIn("static constexpr uint32_t OK          = 0x00DD44;", MK_TUI)
        self.assertIn("static constexpr uint32_t WARN        = 0xFFAA00;", MK_TUI)

    def test_infrared_detail_views_use_the_shared_red_palette(self):
        self.assertIn('#include "../app_common/hp_ui.h"', HEADER)
        self.assertIn("static constexpr uint16_t COL_BG        = hp::COL_BG;", HEADER)
        self.assertIn("static constexpr uint16_t COL_FG        = hp::COL_FG;", HEADER)
        self.assertIn("static constexpr uint16_t COL_HIGHLIGHT = hp::COL_HL;", HEADER)
        self.assertNotIn("0x07E0", HEADER)
        self.assertNotIn("0x2E65", HEADER)

    def test_replay_serialization_round_trips_value_and_bits(self):
        self.assertIn('f.printf("value: 0x%llX\\n", (unsigned long long)sig.value)', SOURCE)
        self.assertIn('f.printf("bits: %u\\n", (unsigned)sig.bits)', SOURCE)
        self.assertIn("bool hasSerializedValue = false;", SOURCE)
        self.assertIn("bool hasSerializedBits = false;", SOURCE)
        self.assertIn("if (!hasSerializedBits) sig.bits = 32;", SOURCE)
        self.assertIn(
            "if (!hasSerializedValue) sig.value = "
            "((uint64_t)sig.address) | ((uint64_t)sig.command << 16);",
            SOURCE,
        )


if __name__ == "__main__":
    unittest.main()
