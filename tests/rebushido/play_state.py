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
    'port_time': 'PortStageChoice.fight_time',
    'port_cursor_time': 'PortStageChoice.cursor_time',
    'port_page_cursor': 'PortStageChoice.page_cursor',
    'port_page_top': 'PortStageChoice.page_top',
    'port_unlock_all': 'PortPlay.unlock_all',
    'port_stage_pages': 'PortPlay.stage_pages',
    'port_page_used': 'PortPlay.page_used',
    'port_unlocks': 'PortPlay.unlocks',
    'port_music_row': 'PortFightMusic.row',
    'port_music_heard': 'PortFightMusic.heard',
    'port_music_track': 'PortFightMusic.track',
    'port_match_page_rule': 'PortMatch.page_rule',
    'port_match_page_count': 'PortMatch.page_count',
    'port_match_rule': 'PortMatch.rule',
    'port_match_count': 'PortMatch.count',
    'port_match_lives_p1': 'PortMatch.lives[0]',
    'port_match_lives_p2': 'PortMatch.lives[1]',
    'port_match_wins_p1': 'PortMatch.wins[0]',
    'port_match_wins_p2': 'PortMatch.wins[1]',
    'port_match_fights': 'PortMatch.fights',
    'port_match_last': 'PortMatch.last',
    'port_match_over': 'PortMatch.over',
    'port_match_winner': 'PortMatch.winner',
    'port_match_card': 'PortMatch.card',
    'port_match_page_crew': 'PortMatch.page_crew',
    'port_match_crew_size': 'PortMatch.crew_size',
    'port_match_crew_picked': 'PortMatch.crew_picked',
    'port_match_crew_sides': 'PortMatch.crew_sides',
    'port_crew_p1_1': 'PortMatch.crew[0][0].fighter',
    'port_crew_p1_2': 'PortMatch.crew[0][1].fighter',
    'port_crew_p1_3': 'PortMatch.crew[0][2].fighter',
    'port_crew_p2_1': 'PortMatch.crew[1][0].fighter',
    'port_crew_p2_2': 'PortMatch.crew[1][1].fighter',
    'port_crew_p2_3': 'PortMatch.crew[1][2].fighter',
    'port_match_rule_page': 'PortMatch.rule_page',
    'port_match_rule_row': 'PortMatch.rule_row',
    'port_match_rule_first': 'PortMatch.rule_first',
    'port_match_crew_most': 'PortMatch.crew_most',
    'port_match_kept_p1': 'PortMatch.wounds_kept[0]',
    'port_match_kept_p2': 'PortMatch.wounds_kept[1]',
    'port_match_off_arm_p1': 'PortMatch.wounds[0][0]',
    'port_match_legs_p1': 'PortMatch.wounds[0][1]',
    'port_match_arm_p1': 'PortMatch.wounds[0][2]',
    'port_match_body_p1': 'PortMatch.wounds[0][3]',
    'port_match_off_arm_p2': 'PortMatch.wounds[1][0]',
    'port_match_legs_p2': 'PortMatch.wounds[1][1]',
    'port_match_arm_p2': 'PortMatch.wounds[1][2]',
    'port_match_body_p2': 'PortMatch.wounds[1][3]',
    'port_match_to_top': 'PortMatch.to_top',
    'port_match_page_teams': 'PortMatch.page_teams',
    'port_match_teams': 'PortMatch.teams',
    'port_match_cpu_row': 'PortMatch.cpu_row',
    'port_match_page_cpu': 'PortMatch.page_cpu',
    'port_match_cpu_taken': 'PortMatch.cpu_taken',
    'port_tournament_phase': 'PortTournament.phase',
    'port_tournament_match': 'PortTournament.match',
    'port_tournament_side1': 'PortTournament.side_entrant[0]',
    'port_tournament_side2': 'PortTournament.side_entrant[1]',
    'port_tournament_decided': 'PortTournament.decided',
    'port_training_on': 'PortTraining.is_on',
    'port_training_recording': 'PortTraining.recording',
    'port_training_playing': 'PortTraining.playing',
    'port_training_frozen': 'PortTraining.frozen',
    'port_training_slow': 'PortTraining.slow',
    'port_training_holding': 'PortTraining.holding',
    'port_training_dummy': 'PortTraining.dummy',
    'port_training_record_at': 'PortTraining.record_at',
    'port_training_play_at': 'PortTraining.play_at',
    'port_training_played_slot': 'PortTraining.played_slot',
    'port_training_frames': 'PortTraining.frames',
    'port_training_restarts': 'PortTraining.restarts',
    'port_training_holds': 'PortTraining.holds',
    'port_training_p1_shown': 'PortTraining.shown[0][0].buttons',
    'port_training_p2_shown': 'PortTraining.shown[1][0].buttons',
    'port_training_guards': 'PortTraining.guards',
    'port_training_let_by': 'PortTraining.let_by',
    'port_training_sweep_p1': 'PortTrainingAreas.sweep_count[0]',
    'port_training_sweep_tris_p1': 'PortTrainingAreas.sweep_triangles[0][0]',
}
# Two on two's state is duo.c's own structure, which no header shows: its
# offsets are those duo.c's PORT_DUO_AT holds the build to, members of
# u8s, each fighter's (`_1` to `_4`) one further.
DUO = os.path.join(support.ROOT, 'src', 'port', 'duo.c')
DUO_NAMES = {'hits': 'hits_taken', 'hit_by': 'last_hit_by'}


def duo_offset(name):
    """The offset duo.c gives state.toml's `port_duo_...` value, or None."""
    with open(DUO, encoding='utf-8') as f:
        at = dict((m, int(o, 0)) for m, o in re.findall(r'^PORT_DUO_AT\((\w+), (\w+)\);', f.read(), re.M))
    key = name[len('port_duo_'):]
    index = 0
    match = re.match(r'(\w+?)_([1-4])$', key)
    if match:
        key, index = match.group(1), int(match.group(2)) - 1
    member = DUO_NAMES.get(key, key)
    return None if member not in at else at[member] + index


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
        if symbol == 'g_port_duo':
            want = duo_offset(name)
            checks.true('state.toml\'s `%s` (%s) is a member duo.c\'s PORT_DUO_AT holds' % (name, place),
                        want is not None)
            if want is not None:
                checks.equal('the offset of state.toml\'s `%s`' % name, offset, want)
            continue
        member = MEMBERS.get(key)
        if member is None:
            checks.true('state.toml\'s `%s` (%s) is a member tests/rebushido/play_state.py knows' % (name, place),
                        False)
            continue
        checks.equal('the offset of %s, state.toml\'s `%s`' % (member, name), offset, where[member] + extra)
