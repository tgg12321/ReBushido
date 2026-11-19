"""The offsets tests/play/state.toml gives for the members of Fighter and
FightConfig, against the structures as the compiler lays them out: a play
test reads the game's memory at those offsets, and would read something
else, silently, if a member moved. Every value of state.toml that has an
offset must be one this test knows."""
import os
import re

import support

STATE = os.path.join(support.ROOT, 'tests', 'play', 'state.toml')
# As a 32-bit program only: the offsets of state.toml are the PlayStation's,
# which a 32-bit program's are too. A 64-bit program lays Fighter out
# otherwise (it has pointers), and the play tests find its members by the
# layout the port's build writes (tools/port_layout.py, console_runs).
BITS = (32,)

# The name in state.toml -> the member play_state.c prints. The second
# fighter's are the first's, one Fighter further.
MEMBERS = {
    'config_roster_p1': 'FightConfig.roster[0]',
    'config_roster_p2': 'FightConfig.roster[1]',
    'config_weapon_p1': 'FightConfig.weapon[0]',
    'config_weapon_p2': 'FightConfig.weapon[1]',
    'config_cpu_p2': 'FightConfig.is_cpu[1]',
    'config_stage': 'FightConfig.stage',
    'config_mode': 'FightConfig.mode',
    'cpu': 'Fighter.is_cpu',
    'character': 'Fighter.character',
    'roster': 'Fighter.roster_id',
    'weapon': 'Fighter.weapon_id',
    'frames_alive': 'Fighter.frames_alive',
    'defeated': 'Fighter.is_defeated',
    'x': 'Fighter.foot_pos.vx',
    'y': 'Fighter.foot_pos.vy',
    'z': 'Fighter.foot_pos.vz',
    'off_arm_ok': 'Fighter.off_arm_ok',
    'leg_wounds': 'Fighter.leg_wounds',
    'arm_wounds': 'Fighter.arm_wounds',
    'body_wounds': 'Fighter.body_wounds',
    'vanish_count': 'Fighter.vanish_count',
    'rb_bugfixes': 'RbSettings.bugfixes',
    'rb_graphics': 'RbSettings.graphics',
    'port_tier': 'PortStageChoice.fight_kind',
    'port_arena': 'PortStageChoice.arena',
    'port_page': 'PortStageChoice.page_open',
    'port_on_stages': 'PortStageChoice.on_stages',
    'port_cursor_tier': 'PortStageChoice.cursor_kind',
}


def check(lines, checks):
    where = {}
    for line in lines:
        name, value = line.split()
        where[name] = int(value)
    with open(STATE, encoding='utf-8') as f:
        entries = re.findall(r'^(\w+)\s*=\s*"([^":]+):(\w+)"', f.read(), re.M)
    checks.true('tests/play/state.toml has values', len(entries) > 0)
    for name, place, _ in entries:
        symbol, _, rest = place.partition('+')
        if not rest:
            continue  # a variable of its own: found by its name alone
        offset = sum(int(part, 0) for part in rest.split('+'))
        key, extra = name, 0
        if symbol == 'g_fighters':
            player, _, key = name.partition('_')
            extra = where['Fighter'] if player == 'p2' else 0
        member = MEMBERS.get(key)
        if member is None:
            checks.true('state.toml\'s `%s` (%s) is a member tests/rebushido/play_state.py knows' % (name, place),
                        False)
            continue
        checks.equal('the offset of %s, state.toml\'s `%s`' % (member, name), offset, where[member] + extra)
