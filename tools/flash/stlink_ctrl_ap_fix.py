"""
Make pyOCD able to program an nRF52840 through an ST-Link.

pyOCD only calls the ST-Link's ``open_ap()`` for memory access ports. Nordic
keeps the debug-protection registers in a proprietary CTRL-AP (access port 1),
which is not a memory AP, so it is never opened. Every access to it then comes
back as ``STLink error (29): Bad AP``, target init fails, and no flashing is
possible - even though the chip is perfectly healthy and unlocked.

Opening the access port before touching it is all that is missing. Importing
this module patches the probe class in memory; the installed pyOCD is not
modified.
"""
from pyocd.probe.stlink_probe import StlinkProbe

APSEL_MASK = 0xFF000000
APSEL_SHIFT = 24

_opened_aps = {}


def _ensure_ap_open(probe, apsel):
    opened = _opened_aps.setdefault(id(probe), set())
    if apsel in opened:
        return
    try:
        probe._link.open_ap(apsel)
    except Exception:
        # Old ST-Link firmware has no multi-AP command. The plain register
        # access below is then the only path available anyway.
        pass
    opened.add(apsel)


def _read_ap(self, addr, now=True):
    apsel = (addr & APSEL_MASK) >> APSEL_SHIFT
    _ensure_ap_open(self, apsel)
    result = self._link.read_dap_register(apsel, addr & 0xFFFF)

    def result_callback():
        return result

    return result if now else result_callback


def _write_ap(self, addr, data):
    apsel = (addr & APSEL_MASK) >> APSEL_SHIFT
    _ensure_ap_open(self, apsel)
    self._link.write_dap_register(apsel, addr & 0xFFFF, data)


StlinkProbe.read_ap = _read_ap
StlinkProbe.write_ap = _write_ap
