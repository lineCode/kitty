#!/usr/bin/env python
# vim:fileencoding=utf-8
# License: GPL v3 Copyright: 2016, Kovid Goyal <kovid at kovidgoyal.net>

from functools import partial

import kitty.fast_data_types as defines
from kitty.keys import (
    interpret_key_event, modify_complex_key, modify_key_bytes, smkx_key_map
)

from . import BaseTest


class DummyWindow:

    def __init__(self):
        self.screen = self
        self.extended_keyboard = False
        self.cursor_key_mode = True


class TestParser(BaseTest):

    def test_modify_complex_key(self):
        self.ae(modify_complex_key('kcuu1', 4), b'\033[1;4A')
        self.ae(modify_complex_key('kcuu1', 3), b'\033[1;3A')
        self.ae(modify_complex_key('kf5', 3), b'\033[15;3~')
        self.assertRaises(ValueError, modify_complex_key, 'kri', 3)

    def test_interpret_key_event(self):
        # test rmkx/smkx
        w = DummyWindow()

        def k(expected, key, mods=0):
            actual = interpret_key_event(
                getattr(defines, 'GLFW_KEY_' + key),
                0,
                mods,
                w,
                defines.GLFW_PRESS,
            )
            self.ae(b'\033' + expected.encode('ascii'), actual)

        for ckm, mch in {True: 'O', False: '['}.items():
            w.cursor_key_mode = ckm
            for name, ch in {
                'UP': 'A',
                'DOWN': 'B',
                'RIGHT': 'C',
                'LEFT': 'D',
                'HOME': 'H',
                'END': 'F',
            }.items():
                k(mch + ch, name)
        w.cursor_key_mode = True

        # test remaining special keys
        for key, num in zip('INSERT DELETE PAGE_UP PAGE_DOWN'.split(), '2356'):
            k('[' + num + '~', key)
        for key, num in zip('1234', 'PQRS'):
            k('O' + num, 'F' + key)
        for key, num in zip(range(5, 13), (15, 17, 18, 19, 20, 21, 23, 24)):
            k('[' + str(num) + '~', 'F{}'.format(key))

        # test modifiers
        SPECIAL_KEYS = 'UP DOWN RIGHT LEFT HOME END INSERT DELETE PAGE_UP PAGE_DOWN '
        for i in range(1, 13):
            SPECIAL_KEYS += 'F{} '.format(i)
        SPECIAL_KEYS = SPECIAL_KEYS.strip().split()
        for mods, num in zip(('CONTROL', 'ALT', 'SHIFT+ALT'), '534'):
            fmods = 0
            num = int(num)
            for m in mods.split('+'):
                fmods |= getattr(defines, 'GLFW_MOD_' + m)
            km = partial(k, mods=fmods)
            for key in SPECIAL_KEYS:
                keycode = getattr(defines, 'GLFW_KEY_' + key)
                base_key = smkx_key_map[keycode]
                km(modify_key_bytes(base_key, num).decode('ascii')[1:], key)
