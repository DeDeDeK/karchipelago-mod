# SPDX-License-Identifier: GPL-3.0-only
# Contains code derived from HSDLib (https://github.com/Ploaj/HSDLib),
# Copyright (c) 2021 Ploaj, used under the MIT License.
# See THIRD_PARTY_NOTICES.md at the repo root for the full MIT license text.
"""Public-symbol-name -> HSD root-type classifier.

Ported from the `symbol_identificators` lambda chain in HSDLib's
HSDRawFile.cs. That chain is a single ordered list of predicates folded
with `?? `, so the FIRST match wins regardless of whether it tests a
prefix, a suffix, or an exact name - `_RULES` below keeps that order
line for line.

HSDLib's final fallthrough (a bare `HSDAccessor`) is reported as None so
the CLI shows a clear "unclassified" hint instead of a wrong guess.
`_KAR_RULES` adds symbol families this repo has identified that HSDLib
does not classify; they run after every HSDLib rule so they can never
shadow one.
"""

from typing import Optional

# (kind, pattern, class-name). kind: 'eq' exact, 'pre' prefix, 'suf'
# suffix, 'pre_suf' (prefix, suffix) pair, 'pre_not' (prefix, excluded
# substring).
_RULES = [
    ("eq", "item_data", "SBM_Article"),
    ("suf", "matanim_joint", "HSD_MatAnimJoint"),
    ("suf", "shapeanim_joint", "HSD_ShapeAnimJoint"),
    ("suf", "_animjoint", "HSD_AnimJoint"),
    ("suf", "_joint", "HSD_JOBJ"),
    ("suf", "_texanim", "HSD_TexAnim"),
    ("suf", "_figatree", "HSD_FigaTree"),
    ("suf", "_camera", "HSD_Camera"),
    ("suf", "_scene_lights", "HSDNullPointerArrayAccessor<HSD_Light>"),
    ("suf", "_scene_models", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("eq", "Stc_rarwmdls", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("eq", "Stc_scemdls", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("eq", "lupe", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("eq", "tdsce", "HSDNullPointerArrayAccessor<HSD_JOBJDesc>"),
    ("suf", "_model_set", "HSD_JOBJDesc"),
    ("suf", "_model_group", "HSD_ModelGroup"),
    ("eq", "ftDataMario", "SBM_ftDataMario"),
    ("eq", "ftDataMars", "SBM_ftDataMars"),
    ("eq", "ftDataEmblem", "SBM_ftDataMars"),
    ("pre_not", ("ftData", "Copy"), "SBM_FighterData"),
    ("suf", "MnSelectChrDataTable", "SBM_SelectChrDataTable"),
    ("suf", "MnSelectStageDataTable", "SBM_MnSelectStageDataTable"),
    ("suf", "coll_data", "SBM_Coll_Data"),
    ("suf", "_fog", "HSD_FogDesc"),
    ("suf", "scene_data", "HSD_SOBJ"),
    ("eq", "pnlsce", "HSD_SOBJ"),
    ("eq", "flmsce", "HSD_SOBJ"),
    ("pre", "Sc", "HSD_SOBJ"),
    ("pre", "map_plit", "HSDNullPointerArrayAccessor<HSD_Light>"),
    ("pre", "map_head", "SBM_Map_Head"),
    ("pre", "grGroundParam", "SBM_GroundParam"),
    ("pre", "vcDataStar", "KAR_vcDataStar"),
    ("pre", "vcDataWheel", "KAR_vcDataWheel"),
    ("pre", "grModelMotion", "HSDArrayAccessor<KAR_grModelMotion>"),
    ("pre", "grModel", "KAR_grModel"),
    ("pre", "grDataCommon", "KAR_grDataCommon"),
    ("pre", "grData", "KAR_grData"),
    ("suf", "_texg", "HSD_TEXGraphicBank"),
    ("suf", "_ptcl", "HSD_ParticleGroup"),
    ("pre", "effBehaviorTable", "MEX_EffectTypeLookup"),
    ("pre", "eff", "SBM_EffectTable"),
    ("pre", "itPublicData", "itPublicData"),
    ("pre", "itemdata", "HSDNullPointerArrayAccessor<SBM_MapItem>"),
    ("pre", "smSoundTestLoadData", "smSoundTestLoadData"),
    ("pre", "ftLoadCommonData", "SBM_ftLoadCommonData"),
    ("pre", "quake_model_set", "SBM_Quake_Model_Set"),
    ("pre", "mexData", "MEX_Data"),
    ("pre", "mexMapData", "MEX_mexMapData"),
    ("pre", "mexSelectChr", "MEX_mexSelectChr"),
    ("pre", "mobj", "HSD_MOBJ"),
    ("pre", "SIS_", "SIS_SdData"),
    ("eq", "evMenu", "SBM_EventMenu"),
    ("suf", "ColAnimData", "HSDArrayAccessor<ftCommonColorEffect>"),
    ("eq", "ftcmd", "SBM_FighterActionTable"),
    ("eq", "Stc_icns", "MEX_Stock"),
    ("eq", "mexMenu", "MEX_Menu"),
    ("eq", "bgm", "MEX_BGMModel"),
    ("eq", "mexCostume", "MEX_CostumeSymbol"),
    ("pre", "mnName", "HSDFixedLengthPointerArrayAccessor<HSD_ShiftJIS_String>"),
    ("suf", "move_logic", "HSDArrayAccessor<MEX_MoveLogic>"),
    ("pre_suf", ("em", "DataGroup"), "KAR_emData"),
    ("eq", "stData", "KAR_stData"),
    ("pre", "rdMotion", "HSDArrayAccessor<KAR_RdMotion>"),
    ("pre", "vcDataCommon", "KAR_vcDataCommon"),
    # HSDLib's own chain shadows this with an earlier `rdDataCommon ->
    # HSDAccessor` TODO stub; KAR_RdDataCommon is the real layout.
    ("pre", "rdDataCommon", "KAR_RdDataCommon"),
    ("pre", "rdData", "KAR_RdData"),
    ("pre", "rdExt", "KEX_RdExt"),
    ("pre", "kexData", "kexData"),
    ("eq", "gmIntroEasyTable", "SBM_gmIntroEasyTable"),
    ("pre", "tyDisplayModel", "HSDArrayAccessor<SBM_tyDisplayModelEntry>"),
    ("pre", "tyModelFile", "HSDArrayAccessor<SBM_TyModelFileEntry>"),
    ("pre", "tyInitModel", "HSDArrayAccessor<SBM_tyInitModelEntry>"),
    ("pre", "tyModelSort", "HSDArrayAccessor<SBM_tyModelSortEntry>"),
    ("pre", "tyExpDifferent", "HSDShortArray"),
    ("pre", "tyNoGetUsTbl", "HSDShortArray"),
    ("pre", "grMurabito", "HSDNullPointerArrayAccessor<SBM_GrMurabito>"),
    ("pre", "itData", "HSDArrayAccessor<KAR_Item>"),
    ("pre", "MemCardBanner", "SBM_MemCardBanner"),
    ("pre", "MemCardIcon", "SBM_MemCardIcon"),
    ("pre", "sss_pages", "AK_StagePages"),
    ("suf", "bitfont", "AK_BitFont"),
    ("suf", "_shape", "AK_Shape"),
    ("suf", "Color", "HSDColorArray"),
    ("suf", "camera_param", "MEX_ResultCameraParam"),
    ("suf", "trophy_icon_param", "SBM_TrophyIcon"),
    ("suf", "ALDYakuAll", "HSDNullPointerArrayAccessor<SBM_ItemSubactionData>"),
    ("suf", "hazard_list", "HSDNullPointerArrayAccessor<HSD_String>"),
    ("suf", "fog_list", "HSDNullPointerArrayAccessor<HSD_FogAnim>"),
    ("suf", "cpu_data", "MEX_CpuData"),
    ("suf", "_tobj", "HSD_TOBJ"),
    ("suf", "allstar_fighters", "HSDArrayAccessor<MEX_AllStarFigther>"),
    ("suf", "hud_colors", "HSDNullPointerArrayAccessor<KAR_HudColor>"),
    ("suf", "_dynamics", "SBM_PhysicsGroup"),
    ("pre", "dbEffectData", "KAR_dbEffectData"),
    ("pre", "smSoundTestFGMGroupTable", "HSDArrayAccessor<KAR_smSoundTestFGMGroupTable>"),
]

# KAR symbol families HSDLib leaves unclassified. Effect banks export four
# symbols per group: <name>_ptcl (generator templates, an HSDLib type),
# <name>_texg (texture bank, ditto), <name>_ref ({u32 base_id; u32 ids[]}
# manifest) and <name>_form (secondary form table keyed to _ref).
# `_cmpatree` is KAR's name for the Melee `_figatree` keyframe container
# (same 0x14 layout); `_image_desc` and this repo's `*Img` publics are bare
# HSD_Image records.
_KAR_RULES = [
    ("suf", "_cmpatree", "HSD_FigaTree"),
    ("suf", "_image_desc", "HSD_Image"),
    ("suf", "Img", "HSD_Image"),
    ("suf", "_ref", "KAR_efRef"),
    ("suf", "_form", "KAR_efForm"),
    ("pre", "efModelData", "HSDArrayAccessor<KAR_EffectModelDesc>"),
]


def _match(name: str, kind: str, pattern) -> bool:
    if kind == "eq":
        return name == pattern
    if kind == "pre":
        return name.startswith(pattern)
    if kind == "suf":
        return name.endswith(pattern)
    if kind == "pre_not":
        return name.startswith(pattern[0]) and pattern[1] not in name
    return name.startswith(pattern[0]) and name.endswith(pattern[1])


def classify_symbol(name: str) -> Optional[str]:
    """Return the HSD root accessor class name for `name`, or None.

    None means "unclassified" - either an unknown public symbol or one
    HSDLib explicitly falls back to a bare HSDAccessor for. The CLI
    treats both cases the same way."""
    for kind, pattern, klass in _RULES:
        if _match(name, kind, pattern):
            return klass
    for kind, pattern, klass in _KAR_RULES:
        if _match(name, kind, pattern):
            return klass
    return None
