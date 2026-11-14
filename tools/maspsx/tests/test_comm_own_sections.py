"""--comm-own-sections: a public tentative definition (`.comm`) gets a section of its own,
.gnu.linkonce.b.<symbol>, for the linker script to place. gp is used as for any COMMON (the base
only); an uninitialized static stays in the file's own .sbss / .bss."""
import unittest

from maspsx import MaspsxProcessor

from .util import strip_comments


def run(lines, **kw):
    mp = MaspsxProcessor(lines, sdata_limit=8, comm_own_sections=True, **kw)
    return strip_comments(mp.process_lines())


BODY = [
    ".ent\tcdrom_SetMix",
    "sb\t$4,g_cd_atv",
    "sb\t$5,g_cd_atv+1",
    ".end\tcdrom_SetMix",
]


class TestCommOwnSections(unittest.TestCase):
    def test_small_comm_gets_its_section_and_alignment(self):
        res = run([".comm\tg_cd_atv,4,4"] + BODY)
        i = res.index('.section .gnu.linkonce.b.g_cd_atv,"aw",@nobits')
        self.assertEqual(
            res[i + 1 : i + 7],
            [".align 2", ".globl g_cd_atv", ".type g_cd_atv,@object", ".size g_cd_atv,4", "g_cd_atv:", ".space 4"],
        )
        self.assertFalse(any(l.startswith(".comm") for l in res))

    def test_alignment_goes_by_size(self):
        for size, align in ((1, None), (2, ".align 1"), (3, ".align 1"), (4, ".align 2"), (8, ".align 3"), (12, ".align 3")):
            res = run([f".comm\tg_x,{size},4"])
            i = res.index('.section .gnu.linkonce.b.g_x,"aw",@nobits')
            if align:
                self.assertEqual(res[i + 1], align)
            else:
                self.assertEqual(res[i + 1], ".globl g_x")

    def test_gp_for_the_base_only(self):
        res = run([".comm\tg_cd_atv,4,4"] + BODY)
        self.assertIn("sb\t$4,%gp_rel(g_cd_atv)($gp)", res)
        self.assertIn("sb\t$5,g_cd_atv+1", res)

    def test_static_stays_in_the_files_sbss(self):
        res = run([".local\tg_cd_atv", ".comm\tg_cd_atv,4,4"] + BODY)
        self.assertFalse(any("linkonce" in l for l in res))
        self.assertIn(".section .sbss", res)
        self.assertIn("g_cd_atv:", res)


if __name__ == "__main__":
    unittest.main()
