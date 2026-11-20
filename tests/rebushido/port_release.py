"""The PC package (tools/port_release.py) refuses whatever is not its own: a disc, a card, a log, a mod made from a disc

The package is made of made-up programs and the repository's own mods,
licences and README, in a folder of its own; then each thing that must
never go to a player (a disc image, a memory card, rebushido.cfg, a log, a
screenshot, the art or HD mods made from a disc, a shipped mod's file
changed or added to, a file the tool did not write) is planted in a copy
of it, and the check must refuse each. Nothing of the game is read."""
import port_release


def run(checks):
    lines = []
    failures = port_release.self_test(report=lines.append)
    checks.true('the clean package passes', any(line.startswith('ok: the clean package') for line in lines))
    for plant in port_release.plants():
        checks.true('%s is refused' % plant[0], not any(f.startswith(plant[0] + ' (') for f in failures))
    checks.equal('the self-test\'s failures', failures, [])
