import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MENU_C = ROOT / "project" / "service" / "menu.c"
MENU_H = ROOT / "project" / "service" / "menu.h"


def _read(path):
    return path.read_text(encoding="utf-8")


def _item_block(source, name):
    marker = f"static const MenuItemDef {name}[] = {{"
    start = source.index(marker) + len(marker)
    end = source.index(";\n\n", start)
    return source[start:end]


def _item_count(source, name):
    return _item_block(source, name).count('{"')


def _without_whitespace(source):
    return re.sub(r"\s+", "", source)


def test_menu_uses_single_table_driven_engine():
    source = _read(MENU_C)
    header = _read(MENU_H)

    assert "typedef struct\n{\n    const char *label;\n    void *target;" in source
    assert "static const MenuPageDef menu_pages[]" in source
    assert "static void Menu_Render_Page(void)" in source
    assert "static void Menu_Handle_Edit(uint8 event_code)" in source
    assert "static void Menu_Handle_Navigation(uint8 event_code)" in source

    assert "display_codename" not in source
    assert "display_codename" not in header
    assert "menu_have_sub" not in source
    assert "Menu_Have_Sub_Menu" not in source
    assert "Menu_Yuanshu_Process" not in source
    assert "Menu_Process_Float_Value" not in source
    assert "Menu_Process_Int_Value" not in source


def test_menu_page_counts_and_parent_links_are_complete():
    source = _read(MENU_C)
    compact_source = _without_whitespace(source)

    expected_counts = {
        "menu_home_items": 6,
        "menu_start_items": 6,
        "menu_speed_items": 6,
        "menu_model_items": 6,
        "menu_diff_items": 3,
        "menu_yuanshu_items": 5,
        "menu_ring_items": 3,
        "menu_ring_p0_items": 3,
        "menu_ring_p1_items": 3,
        "menu_ring_entry_items": 6,
        "menu_ring_in_items": 6,
        "menu_ring_out_items": 6,
        "menu_ring_adc_items": 6,
        "menu_ring_drive_items": 4,
        "menu_ring_p1_drive_items": 4,
        "menu_cylinder_items": 6,
        "menu_wall_items": 4,
        "menu_fly_items": 6,
        "menu_seesaw_items": 6,
        "menu_cross_items": 4,
        "menu_element_len_items": 1,
        "menu_element_items": 6,
    }
    for name, count in expected_counts.items():
        assert _item_count(source, name) == count

    assert '{"WALL",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_WALL}' in compact_source
    assert '{"CROSS",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_CROSS}' in compact_source
    assert '{"DIFF",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_DIFF}' in compact_source
    assert '{"<<WALL",menu_wall_items,MENU_ITEM_COUNT(menu_wall_items),MENU_PAGE_YUANSHU}' in compact_source
    assert '{"<<CROSS",menu_cross_items,MENU_ITEM_COUNT(menu_cross_items),MENU_PAGE_YUANSHU}' in compact_source
    assert '{"<<DIFF",menu_diff_items,MENU_ITEM_COUNT(menu_diff_items),MENU_PAGE_HOME}' in compact_source
    assert (
        '{"<<P0_ENTRY",menu_ring_entry_items,'
        'MENU_ITEM_COUNT(menu_ring_entry_items),MENU_PAGE_RING_P0}' in compact_source
    )
    assert (
        '{"<<P0_CTRL",menu_ring_in_items,'
        'MENU_ITEM_COUNT(menu_ring_in_items),MENU_PAGE_RING_P0}' in compact_source
    )
    assert (
        '{"<<P1_ENTRY",menu_ring_out_items,'
        'MENU_ITEM_COUNT(menu_ring_out_items),MENU_PAGE_RING_P1}' in compact_source
    )
    assert (
        '{"<<P1_CTRL",menu_ring_adc_items,'
        'MENU_ITEM_COUNT(menu_ring_adc_items),MENU_PAGE_RING_P1}' in compact_source
    )
    assert '{"<<P0",menu_ring_p0_items,MENU_ITEM_COUNT(menu_ring_p0_items),MENU_PAGE_RING}' in compact_source
    assert '{"<<P1",menu_ring_p1_items,MENU_ITEM_COUNT(menu_ring_p1_items),MENU_PAGE_RING}' in compact_source
    assert (
        '{"<<P0_DRIVE",menu_ring_drive_items,'
        'MENU_ITEM_COUNT(menu_ring_drive_items),MENU_PAGE_RING_P0}' in compact_source
    )
    assert (
        '{"<<P1_DRIVE",menu_ring_p1_drive_items,'
        'MENU_ITEM_COUNT(menu_ring_p1_drive_items),MENU_PAGE_RING_P1}' in compact_source
    )
    assert '{"<<ENTRY",menu_ring_entry_items,MENU_ITEM_COUNT(menu_ring_entry_items),MENU_PAGE_RING}' in compact_source
    assert '{"<<IN_RING",menu_ring_in_items,MENU_ITEM_COUNT(menu_ring_in_items),MENU_PAGE_RING}' in compact_source
    assert '{"<<OUT_RING",menu_ring_out_items,MENU_ITEM_COUNT(menu_ring_out_items),MENU_PAGE_RING}' in compact_source
    assert '{"<<ELEM",menu_element_len_items,MENU_ITEM_COUNT(menu_element_len_items),MENU_PAGE_START}' in compact_source
    assert '{"<<ELEM",menu_element_items,MENU_ITEM_COUNT(menu_element_items),MENU_PAGE_ELEMENT_LEN}' in compact_source


def test_menu_parameter_steps_match_tuning_contract():
    source = _read(MENU_C)

    speed = _item_block(source, "menu_speed_items")
    assert [
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_0001",
        "MENU_FLOAT_STEP_1",
        "MENU_FLOAT_STEP_1",
        "MENU_FLOAT_STEP_0001",
    ] == re.findall(r"MENU_FLOAT_STEP_[0-9]+", speed)

    model = _item_block(source, "menu_model_items")
    assert [
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_1",
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_001",
        "MENU_FLOAT_STEP_001",
    ] == re.findall(r"MENU_FLOAT_STEP_[0-9]+", model)

    cylinder = _item_block(source, "menu_cylinder_items")
    assert '"exit_spd", &app.cylinder.exit_slow_speed' in cylinder
    assert '"exit_dist"' not in cylinder
    assert "MENU_META(MENU_ITEM_INT16, 4, 0), MENU_INT_STEP_5" in cylinder

    wall = _item_block(source, "menu_wall_items")
    assert "MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_5" in wall
    assert wall.count("MENU_META(MENU_ITEM_INT16, 3, 0), MENU_INT_STEP_10") == 2
    assert "MENU_META(MENU_ITEM_FLOAT, 4, 1), MENU_FLOAT_STEP_1" in wall

    cross = _item_block(source, "menu_cross_items")
    assert cross.count("MENU_FLOAT_STEP_01") == 3
    assert "MENU_FLOAT_STEP_1" in cross


def test_diff_menu_exposes_switch_and_both_gains():
    source = _read(MENU_C)
    diff = _item_block(source, "menu_diff_items")

    assert '"diff_en", &app.speed.diff_enable' in diff
    assert "MENU_META(MENU_ITEM_BOOL, 1, 0), 0" in diff
    assert '"inner_g", &app.speed.diff_inner_gain' in diff
    assert '"outer_g", &app.speed.diff_outer_gain' in diff
    assert diff.count("MENU_FLOAT_STEP_01") == 2


def test_start_menu_exposes_encoder_stop_distance_in_centimeters():
    source = _read(MENU_C)
    start = _item_block(source, "menu_start_items")

    assert '"stop_cm", &app.start.encoder_stop_distance_cm' in start
    assert "MENU_META(MENU_ITEM_FLOAT, 4, 0), MENU_FLOAT_STEP_10" in start


def test_ring_menu_selects_sensor_bias_or_legacy_subpages_at_compile_time():
    source = _read(MENU_C)
    compact_source = _without_whitespace(source)
    ring = _item_block(source, "menu_ring_items")
    entry = _item_block(source, "menu_ring_entry_items")
    ring_in = _item_block(source, "menu_ring_in_items")
    ring_out = _item_block(source, "menu_ring_out_items")

    assert "#if RING_CONTROL_MODE == RING_MODE_SENSOR_BIAS" in source
    assert '"profile",&app.ring.profile_select,MENU_META(MENU_ITEM_BOOL,1,0),0' in compact_source
    assert '{"P0",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_P0}' in compact_source
    assert '{"P1",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_P1}' in compact_source
    assert '{"DRIVE",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_DRIVE}' in compact_source
    assert '"gain", &app.ring.profiles[0].bias_entry_gain' in entry
    assert '"exit_gain", &app.ring.profiles[0].bias_exit_gain' in entry
    assert '"finish_E", &app.ring.profiles[0].bias_finish_encoder' in entry
    assert '"ring_spd", &app.ring.profiles[0].target_speed' in entry
    assert '"adc_a_1", &app.ring.profiles[0].adc_a_1' in ring_in
    assert '"gain", &app.ring.profiles[1].bias_entry_gain' in ring_out
    assert '"exit_gain", &app.ring.profiles[1].bias_exit_gain' in ring_out
    assert '"ring_spd", &app.ring.profiles[1].target_speed' in ring_out
    assert '"kp_Ang", &app.ring.profiles[0].kp_Angle' in source
    assert '"kp_Ang", &app.ring.profiles[1].kp_Angle' in source

    assert '{"ENTRY",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_ENTRY}' in compact_source
    assert '{"IN_RING",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_IN}' in compact_source
    assert '{"OUT_RING",0,MENU_META(MENU_ITEM_LINK,0,0),MENU_PAGE_RING_OUT}' in compact_source
    assert '"entry_E", &app.ring.ring_entry_encoder' in source
    assert '"pre_r_T", &app.ring.pre_ring_Gyro_target' in source
    assert '"pre_r_Gz", &app.ring.pre_ring_Gyroz' in source
    assert '"in_r_Gz", &app.ring.in_ring_Gyroz' in source
    assert '"in_r_E", &app.ring.in_ring_encoder' in source
    assert '"pre_o_T", &app.ring.pre_out_ring_Gyro_target' in source
    assert '"pre_o_Gz", &app.ring.pre_out_ring_Gyroz' in source
    assert '"drv_o_E", &app.ring.drive_out_ring_encoder' in source
    assert source.count("&app.ring.") == 51
    assert "ring_data.yaw_delta_sum" not in source
    assert "ring_data.encoder" not in source


def test_menu_integer_steps_are_named_and_source_is_consistently_wrapped():
    source = _read(MENU_C)

    assert re.search(
        r"typedef enum\s*\{\s*"
        r"MENU_INT_STEP_1\s*=\s*1,\s*"
        r"MENU_INT_STEP_5\s*=\s*5,\s*"
        r"MENU_INT_STEP_10\s*=\s*10\s*"
        r"\}\s*MenuIntStep;",
        source,
    )
    assert not re.search(
        r"MENU_META\(MENU_ITEM_INT16,[^)]*\),\s*(?:1|5|10)\s*\}",
        source,
    )
    assert all(len(line) <= 100 for line in source.splitlines())
    assert not re.search(r"#define MENU_META\([^\n]+\)\s+\+", source)


def test_menu_preserves_edit_and_special_page_semantics():
    source = _read(MENU_C)

    assert "*int_value = increase ? 1 : 0;" in source
    assert "menu_change_multiplier = (menu_change_multiplier == 1) ? 10u" in source
    assert "((menu_change_multiplier == 10) ? 100u : 1u);" in source
    assert "*int_value >= TRACK_ELEMENT_SEQUENCE_MAX" in source
    assert "*int_value <= 1" in source
    assert "if (type == MENU_ITEM_LENGTH)" in source
    assert "Menu_Change_Page(item->action);" in source
    assert "menu_editing = (page == MENU_PAGE_ELEMENT_LEN) ? 1u : 0u;" in source
    assert "page->item_count == 0" in source
    assert "config_save();" in source
    assert "Menu_Show_Save_Prompt();" in source


def test_fly_and_seesaw_modes_have_separate_row_bindings():
    source = _read(MENU_C)
    fly = _item_block(source, "menu_fly_items")
    seesaw = _item_block(source, "menu_seesaw_items")

    assert '"fly_speed", &app.fly.fly_speed' in fly
    assert '"recover_spd", &app.fly.fly_recover_speed' in fly
    assert '"land_cnt", &app.fly.fly_land_confirm_count' in fly
    assert '"seesaw_spd", &app.fly.seesaw_speed' in seesaw
    assert '"wait_cnt", &app.fly.seesaw_wait_count' in seesaw
    assert '"creep_cm", &app.fly.seesaw_creep_cm' in seesaw
    assert "if (menu_page == MENU_PAGE_FLY && app.fly.seesaw_mode != 0)" in source
