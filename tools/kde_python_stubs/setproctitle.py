"""Tiny local setproctitle shim for kde-builder.

kde-builder only needs the module to exist on some hosts. Ridux keeps this
stub local so the Plasma source workflow does not depend on installing a
system-wide Python package in WSL.
"""

def setproctitle(_title):
    return None


def getproctitle():
    return "kde-builder"
