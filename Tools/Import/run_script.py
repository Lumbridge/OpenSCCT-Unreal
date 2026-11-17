"""Runs another editor Python script and says plainly whether it finished. Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="<full path>/run_script.py <full path>/<script>.py [arguments]" -nullrhi
(forward slashes in every path: the script switch reads a backslash as an escape)

The commandlet's exit code cannot be trusted for this: it is 1 whenever anything in the engine logged an error
while starting (the toolset plugins do in a commandlet), whether or not the script worked, and it is 0 for some
scripts that raised. So the last line this writes is the answer, and Tools/EditorScript.ps1 reads it:

    SCCT-SCRIPT OK <script>.py
    SCCT-SCRIPT FAILED <script>.py: <the error>
"""
import os
import runpy
import sys
import traceback

import unreal

target = sys.argv[1]
name = os.path.basename(target)
sys.argv = [target] + sys.argv[2:]
# The scripts import their neighbours (scct_materials, scct_fx...).
folder = os.path.dirname(os.path.abspath(target))
if folder not in sys.path:
    sys.path.insert(0, folder)

try:
    runpy.run_path(target, run_name="__main__")
except SystemExit as stop:
    if stop.code in (None, 0):
        unreal.log_warning("SCCT-SCRIPT OK " + name)
    else:
        unreal.log_warning("SCCT-SCRIPT FAILED %s: exit %s" % (name, stop.code))
except BaseException as error:  # pylint: disable=broad-except
    for line in traceback.format_exc().splitlines():
        unreal.log_warning("SCCT-SCRIPT | " + line)
    unreal.log_warning("SCCT-SCRIPT FAILED %s: %s: %s" % (name, type(error).__name__, error))
else:
    unreal.log_warning("SCCT-SCRIPT OK " + name)
