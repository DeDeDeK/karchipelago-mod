# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Public-symbol-name -> HSD root-type classifier.

Ported from the StartsWith dispatch table in HSDLib's HSDRawFile.cs.
Used to annotate `ls` output and (in the future) route the tree walker
to the right starting type for non-JObj roots.

Only the AirRide / common types we actually encounter are listed; bare
HSDAccessor fallthroughs from HSDLib (every other prefix) are reported
as None so the CLI shows a clear "unclassified" hint instead of a wrong
guess.
"""

from typing import Optional

# Order matters: longest/most-specific prefix first. The first match wins,
# matching HSDLib's lambda-chain semantics.
_PREFIX_TABLE = [
    # AirRide stage/model
    ("grModelMotion", "KAR_grModelMotion"),
    ("grModel", "KAR_grModel"),  # ModelSection (JOBJDesc**[4])
    ("grDataCommon", "KAR_grDataCommon"),
    ("grData", "KAR_grData"),
    ("grGroundParam", "SBM_GroundParam"),
    ("grMurabito", "HSDNullPointerArrayAccessor<SBM_GrMurabito>"),
    # AirRide vehicles / riders
    ("vcDataStar", "KAR_vcDataStar"),
    ("vcDataWheel", "KAR_vcDataWheel"),
    ("vcDataCommon", "KAR_vcDataCommon"),
    ("rdMotion", "HSDArrayAccessor<KAR_RdMotion>"),
    ("rdDataCommon", "HSDAccessor"),  # TODO in HSDLib
    ("rdData", "KAR_RdData"),
    ("rdExt", "KEX_RdExt"),
    ("kexData", "kexData"),
    # AirRide items / weapons
    ("itPublicData", "itPublicData"),
    ("itemdata", "HSDNullPointerArrayAccessor<SBM_MapItem>"),
    ("itData", "HSDArrayAccessor<KAR_Item>"),
    # Melee-leaning (still appear in some shared archives)
    ("ftData", "SBM_FighterData"),  # !Copy
    ("Sc", "HSD_SOBJ"),
    ("map_plit", "HSDNullPointerArrayAccessor<HSD_Light>"),
    ("map_head", "SBM_Map_Head"),
    ("effBehaviorTable", "MEX_EffectTypeLookup"),
    ("eff", "SBM_EffectTable"),
    ("smSoundTestLoadData", "smSoundTestLoadData"),
    ("ftLoadCommonData", "SBM_ftLoadCommonData"),
    ("quake_model_set", "SBM_Quake_Model_Set"),
    ("mexMapData", "MEX_mexMapData"),
    ("mexSelectChr", "MEX_mexSelectChr"),
    ("mexData", "MEX_Data"),
    ("mobj", "HSD_MOBJ"),
    ("SIS_", "SIS_SdData"),
    ("mnName", "HSDFixedLengthPointerArrayAccessor<HSD_ShiftJIS_String>"),
    ("tyDisplayModel", "HSDArrayAccessor<SBM_tyDisplayModelEntry>"),
    ("tyModelFile", "HSDArrayAccessor<SBM_TyModelFileEntry>"),
    ("tyInitModel", "HSDArrayAccessor<SBM_tyInitModelEntry>"),
    ("tyModelSort", "HSDArrayAccessor<SBM_tyModelSortEntry>"),
    ("tyExpDifferent", "HSDShortArray"),
    ("tyNoGetUsTbl", "HSDShortArray"),
    ("MemCardBanner", "SBM_MemCardBanner"),
]

# Suffix-shaped matches. These win over the prefix table to match
# HSDLib's ordering (see HSDRawFile.cs where the suffix lambda is
# registered before the Sc -> HSD_SOBJ prefix). Without this, every
# Sc*_camera public misclassifies as SOBJ.
# Order matches HSDLib's lambda chain in HSDRawFile.cs -- the first match
# wins so e.g. "matanim_joint" doesn't degrade to a bare HSD_JOBJ.
_SUFFIX_TABLE = [
    ("matanim_joint", "HSD_MatAnimJoint"),
    ("shapeanim_joint", "HSD_ShapeAnimJoint"),
    ("_animjoint", "HSD_AnimJoint"),
    ("_joint", "HSD_JOBJ"),
    ("_texanim", "HSD_TexAnim"),
    ("_figatree", "HSD_FigaTree"),
    ("_camera", "HSD_Camera"),
    ("_scene_lights", "HSDNullPointerArrayAccessor<HSD_Light>"),
    ("_scene_models", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("_model_set", "HSD_JOBJDesc"),
    ("_model_group", "HSD_ModelGroup"),
    ("_fog", "HSD_FogDesc"),
    ("_texg", "HSD_TEXGraphicBank"),
    ("_ptcl", "HSD_ParticleGroup"),
    ("camera_param", "MEX_ResultCameraParam"),
]


def _suffix_match(name: str) -> Optional[str]:
    for suffix, klass in _SUFFIX_TABLE:
        if name.endswith(suffix):
            return klass
    if name.startswith("em") and name.endswith("DataGroup"):
        return "KAR_emData"
    return None


def classify_symbol(name: str) -> Optional[str]:
    """Return the HSDLib root accessor class name for `name`, or None.

    None means "unclassified" - either an unknown public symbol or one
    HSDLib explicitly falls back to a bare HSDAccessor for. The CLI
    treats both cases the same way."""
    suffix = _suffix_match(name)
    if suffix is not None:
        return suffix
    for prefix, klass in _PREFIX_TABLE:
        if name.startswith(prefix):
            return klass
    return None
