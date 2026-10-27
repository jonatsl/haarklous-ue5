"""Apply tools/unreal/look.py to the existing level /Game/Maps/Torshov (sun, sky light, post-process volume) without
placing anything again, so the baked lighting stays valid. Run headless with tools/unreal/run-python.ps1."""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import look  # noqa: E402

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

LES.load_level('/Game/Maps/Torshov')
done = []
for a in EAS.get_all_level_actors():
    label = a.get_actor_label()
    if label == 'Sun':
        look.sun(a.light_component); done.append(label)
    elif label == 'Sky light':
        look.sky_light(a.light_component); done.append(label)
    elif label == 'Post process':
        a.settings = look.post(a.settings); done.append(label)
LES.save_current_level()
look.log('look applied to', ', '.join(done))
