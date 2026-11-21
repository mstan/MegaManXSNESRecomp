import importlib.util
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location("coop_hooks",Path(__file__).resolve().parents[1]/"tools/apply_coop_hooks.py")
hooks=importlib.util.module_from_spec(spec);spec.loader.exec_module(hooks)

class NativeEntryHooks(unittest.TestCase):
    def test_bank_and_named_entries_keep_their_real_identity(self):
        for name in ("bank_81_DD2D_M1X1","diz_eventID__12_state_00_M1X1"):
            text=(f"RecompReturn {name}(CpuState *cpu) {{\n"
                  f"  cpu_trace_func_entry(cpu, 0x81DD2D, \"{name}\");\n"
                  "  adopt_return_context(cpu);\n"
                  "  if (g_recomp_stack_top >= 1) g_cpu_entry_s[g_recomp_stack_top - 1] = _entry_s;\n"
                  "  native_body(cpu);\n}\n")
            result,found=hooks.apply(text)
            self.assertEqual(found,{0x01dd2d})
            self.assertEqual(result.count(hooks.MARKER),1)
            self.assertIn("0x81dd2du",result)
            self.assertGreater(result.index(hooks.MARKER),result.index("adopt_return_context"))
            self.assertLess(result.index(hooks.MARKER),result.index("native_body"))
            self.assertEqual(hooks.apply(result)[0],result)

if __name__=="__main__":unittest.main()
