"""Structural checks for the standalone direct-bus AM4 HC1200 project."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
BOARD = ROOT / "boards/hc1200-microcomp"


class StandaloneAm4Project(unittest.TestCase):
    def test_diamond_project_contains_only_active_direct_bus_sources(self):
        project = ET.parse(BOARD / "microcomp-am4.ldf").getroot()
        impl = next(item for item in project.findall("Implementation")
                    if item.get("title") == project.get("default_implementation"))
        sources = {source.get("name") for source in impl.findall("Source")
                   if source.get("type") == "Verilog"}
        self.assertEqual(impl.find("Options").get("def_top"),
                         "am4_hc1200_microcomp")
        self.assertIn("../../rtl/experimental/am4/am4_direct.v", sources)
        self.assertNotIn("../../rtl/experimental/am4/am4_wb.v", sources)
        self.assertIn("am4_mcrom_ebr.v", sources)
        self.assertIn("../../rtl/spi_fram_guest_ram.v", sources)
        self.assertIn("../../rtl/spi_byte_service.v", sources)

    def test_direct_interface_and_board_devices_are_present(self):
        direct = (ROOT / "rtl/experimental/am4/am4_direct.v").read_text()
        board = (BOARD / "am4_cpu11_bus.v").read_text()
        top = (BOARD / "am4_microcomp.v").read_text()
        self.assertIn("output         bus_request", direct)
        self.assertIn("input          bus_ready", direct)
        self.assertNotIn("wbm_", direct)
        self.assertIn("am4_direct #", top)
        self.assertIn(".SD_BOOT_ENABLE(1)", top)
        self.assertIn(".RK_SERVICE_ENABLE(1)", top)
        self.assertIn("localparam [15:0] KL11_BASE = 16'o177560", board)
        self.assertIn("localparam [15:0] LTC_CSR = 16'o177546", board)
        self.assertIn("localparam [15:0] PANEL_BASE = 16'o166000", board)
        self.assertNotIn("timer_armed", board)
        self.assertIn(".panel_key_rows(gpio_key_row)", top)
        self.assertIn(".panel_reg_latch(gpio_reg_latch)", top)
        self.assertIn("localparam [15:0] SD_BASE = 16'o177500", board)
        self.assertIn("localparam [15:0] RK_BASE = 16'o177440", board)
        self.assertIn("spi_fram_guest_ram", board)
        self.assertIn("spi_byte_service", board)
        self.assertIn("wbc_uart_xo2", board)

    def test_generated_control_store_is_exactly_seven_ebrs(self):
        generated = (BOARD / "am4_mcrom_ebr.v").read_text()
        self.assertEqual(generated.count("    DP8KC #("), 7)
        self.assertEqual(generated.count(".DATA_WIDTH_A(9)"), 7)
        self.assertIn(".DOB8(boot_data[6])", generated)
        self.assertNotIn("$readmemh", generated)

    def test_build_tcl_rejects_negative_slack_before_export(self):
        tclsh = shutil.which("tclsh")
        self.assertIsNotNone(tclsh)
        harness = r'''
proc prj_project {args} { puts "PROJECT $args" }
proc prj_run {args} { puts "RUN $args" }
rename open real_open
proc open {path mode} { return [real_open $::env(TEST_TRACE) r] }
source $::env(TEST_SCRIPT)
'''
        with tempfile.TemporaryDirectory(prefix="lsi11-fpga-project-") as directory:
            report = Path(directory) / "timing.twr"
            for content, success in (("Cumulative negative slack: 0.000\n" * 2, True),
                                     ("Cumulative negative slack: -0.1\n", False)):
                report.write_text(content)
                env = dict(os.environ, TEST_TRACE=str(report),
                           TEST_SCRIPT=str(BOARD / "build-am4.tcl"))
                result = subprocess.run([tclsh], input=harness, text=True,
                                        capture_output=True, env=env, check=False)
                self.assertEqual(result.returncode == 0, success, result.stderr)
                self.assertEqual("RUN Export -impl impl1 -task Jedecgen" in result.stdout,
                                 success)


if __name__ == "__main__":
    unittest.main()
