import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TPL_C = ROOT / "project" / "service" / "tpl0102.c"
TPL_H = ROOT / "project" / "service" / "tpl0102.h"
MENU_C = ROOT / "project" / "service" / "menu.c"
INT_USER_C = ROOT / "project" / "user" / "int_user.c"
UVPROJ = ROOT / "project" / "mdk" / "seekfree.uvproj"


def _read(path):
    return path.read_text(encoding="utf-8")


def _compact(source):
    return re.sub(r"\s+", "", source)


def test_tpl0102_uses_verified_soft_iic_transport_and_addresses():
    source = _read(TPL_C)
    header = _read(TPL_H)

    assert "tpl0102_iic_start" in source
    assert "tpl0102_iic_stop" in source
    assert "tpl0102_iic_write_byte" in source
    assert "tpl0102_iic_read_byte" in source
    assert "TPL0102_ADDR_54 0x54u" in header
    assert "TPL0102_ADDR_56 0x56u" in header
    assert "P34" in source
    assert "P35" in source
    assert "P3M0" in source
    assert "P3M1" in source


def test_tpl0102_adjustment_writes_volatile_register_and_save_is_explicit():
    source = _read(TPL_C)
    compact = _compact(source)

    assert "uint8tpl0102_set_a(uint8device,uint8value)" in compact
    assert "uint8tpl0102_set_b(uint8device,uint8value)" in compact
    assert "tpl0102_write_volatile(device,TPL0102_REG_WRA,value)" in compact
    assert "tpl0102_write_volatile(device,TPL0102_REG_WRB,value)" in compact
    assert "uint8tpl0102_save(uint8device)" in compact
    assert "TPL0102_ACR_NONVOLATILE_ENABLE" in source
    assert "tpl0102_write_ab" in source


def test_tpl0102_menu_is_flat_under_sensor_and_writes_immediately():
    source = _read(MENU_C)
    compact = _compact(source)

    assert "MENU_PAGE_TPL" in source
    assert "MENU_PAGE_TPL_54" not in source
    assert "MENU_PAGE_TPL_56" not in source
    assert "MENU_ITEM_TPL_CHANNEL" in source
    assert "MENU_ITEM_TPL_SAVE" in source
    assert '{"AD1",0,MENU_META(MENU_ITEM_TPL_CHANNEL,3,0),MENU_TPL_CHANNEL_AD1}' in compact
    assert '{"AD2",0,MENU_META(MENU_ITEM_TPL_CHANNEL,3,0),MENU_TPL_CHANNEL_AD2}' in compact
    assert '{"AD3",0,MENU_META(MENU_ITEM_TPL_CHANNEL,3,0),MENU_TPL_CHANNEL_AD3}' in compact
    assert '{"AD4",0,MENU_META(MENU_ITEM_TPL_CHANNEL,3,0),MENU_TPL_CHANNEL_AD4}' in compact
    assert '{"SAVE",0,MENU_META(MENU_ITEM_TPL_SAVE,0,0),0}' in compact
    assert '{"<<TPL",menu_tpl_items,MENU_ITEM_COUNT(menu_tpl_items),MENU_PAGE_SENSOR}' in compact
    assert "menu_page == MENU_PAGE_SENSOR && event_code == KEYSTROKE_THREE" in source
    assert "Menu_Change_Page(MENU_PAGE_TPL);" in source
    assert "Menu_Apply_Tpl_Channel_Change" in source
    assert "tpl0102_set_a" in source
    assert "tpl0102_set_b" in source
    assert "MENU_TPL_CHANNEL_AD1" in source
    assert "MENU_TPL_CHANNEL_AD4" in source
    assert "return ad1;" in source
    assert "return ad2;" in source
    assert "return ad3;" in source
    assert "return ad4;" in source
    assert "Menu_Get_Tpl_Adc_Index" not in source
    assert "RAW[Menu_Get_Tpl" not in source
    assert "!tpl0102_online(Menu_Get_Tpl_Channel_Device(item->action))" in source
    assert "save_54 = tpl0102_save(TPL0102_DEVICE_54);" in source
    assert "save_56 = tpl0102_save(TPL0102_DEVICE_56);" in source
    assert "Menu_Show_Tpl_Save_Result(save_54 && save_56);" in source
    render = source[source.index("static void Menu_Render_Page(void)"):]
    render = render[:render.index("static void Menu_Change_Page(uint8 page)")]
    assert "tpl0102_refresh(" not in render
    change_page = source[source.index("static void Menu_Change_Page(uint8 page)"):]
    assert "tpl0102_refresh(TPL0102_DEVICE_54);" in change_page
    assert "tpl0102_refresh(TPL0102_DEVICE_56);" in change_page


def test_tpl0102_initializes_before_control_timers_and_is_in_keil_project():
    int_user = _read(INT_USER_C)
    project = _read(UVPROJ)

    assert "tpl0102_init(TPL0102_DEVICE_54);" in int_user
    assert "tpl0102_init(TPL0102_DEVICE_56);" in int_user
    assert "hardware_init();" in int_user
    assert "hardware_init();" in int_user[: int_user.index("pit_ms_init(TIM0_PIT, TIME_0);")]
    assert "<FileName>tpl0102.c</FileName>" in project
    assert "<FileName>tpl0102.h</FileName>" in project


def test_global_save_persists_vehicle_config_and_both_tpl_devices():
    source = _read(INT_USER_C)
    config_save = source[source.index("void config_save(void)"):]
    config_save = config_save[: config_save.index("/**", 1)]

    assert config_save.index("control_apply_config();") < config_save.index("eeprom_flash();")
    assert config_save.index("eeprom_flash();") < config_save.index(
        "tpl0102_save(TPL0102_DEVICE_54);")
    assert config_save.index("tpl0102_save(TPL0102_DEVICE_54);") < config_save.index(
        "tpl0102_save(TPL0102_DEVICE_56);")
