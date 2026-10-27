"""Runs Time Lock's offline tests against the real game files (read only).

  python tests/run_tests.py GAME_MOUNT_DIR
  (or set FH2_MOUNT=GAME_MOUNT_DIR and run it without an argument)

GAME_MOUNT_DIR is your own Forza Horizon 2 install's Mount folder, the one that
holds forza_durango_release_final.exe and media/UI.zip. Nothing from the game
is included in this repository; tests/work is generated and git-ignored.

Extracts A_VideoOptions.xaml (and other option pages) from media/UI.zip
into tests/work and makes edited copies:
  - A_VideoOptions_eo_sim:  Extra Options-style edits, reconstructed from the
    anchors and inserts in its DLL's strings: the brightness bar re-centred,
    its own resources, FOV slider and shadows rows, list Height 110 -> 275,
    description panel Height 100 -> 110.
  - A_VideoOptions_other:   another mod's edit: an extra row, an extra resource
    with its own nested ResourceDictionary, a different list height.
  - A_HUDOptions_platform:  the HUD page edited by another mod (the FH2 Mod
    Platform edits that page); Time Lock must leave every HUD page alone.
It then runs tests/offline_test.exe (build it with build.cmd first) and checks
each edited Video page is well-formed XML with our rows last in OptionsList,
our resources in Page.Resources, and every StaticResource we reference defined
in the game's UI.
"""
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
# The game's Mount folder (the one holding forza_durango_release_final.exe and media\UI.zip):
# the first argument, else the FH2_MOUNT environment variable. Nothing from the game ships
# with this repository; the tests read your own install, read only.
MOUNT = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("FH2_MOUNT", "")
WORK = os.path.join(HERE, "work")  # generated copies of your game's pages: git-ignored, never commit it
X = "{http://schemas.microsoft.com/winfx/2006/xaml}"
W = "{http://schemas.microsoft.com/winfx/2006/xaml/presentation}"
L = "{clr-namespace:ForzaUI}"
ROW_INDENT = "\r\n                                            "


def once(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def main():
    if not MOUNT or not os.path.isfile(os.path.join(MOUNT, "forza_durango_release_final.exe")):
        sys.exit("Pass your FH2 Mount folder (the one with forza_durango_release_final.exe and media\\UI.zip) "
                 "as the first argument, or set FH2_MOUNT.")
    os.makedirs(WORK, exist_ok=True)
    for stale in os.listdir(WORK):
        os.remove(os.path.join(WORK, stale))
    ui = zipfile.ZipFile(os.path.join(MOUNT, "media", "UI.zip"))
    pages = {}
    for name in ("A_VideoOptions", "A_HUDOptions", "A_AudioOptions", "A_PauseOptions"):
        pages[name] = os.path.join(WORK, name + ".xaml")
        open(pages[name], "wb").write(ui.read(f"Scenes/Anthem/{name}.xaml"))
    video = open(pages["A_VideoOptions"], "rb").read().decode()

    eo = once(video, '<StackPanel Margin="0, 0, 0, 6" Orientation="Horizontal" Style="{StaticResource A_Style_FrameworkElement_DefaultTransition_0}" Background="Black">',
              '<StackPanel Width="806" HorizontalAlignment="Center" Margin="0, 0, 0, 6" Orientation="Horizontal" Style="{StaticResource A_Style_FrameworkElement_DefaultTransition_0}" Background="Black">')
    eo = once(eo, "        </ResourceDictionary>\r\n    </Page.Resources>",
              '            <local:TileMenuDataItem x:Key="EO_FovChase" Heading="Chase FOV" Description="Field of view."/>\r\n'
              '            <local:TileMenuDataItem x:Key="EO_RealtimeShadows" Heading="Realtime Shadows" Description="Shadows."/>\r\n'
              '            <local:NumUnitConverter x:Key="EO_FovDegrees" FractionDigits="1" ShowUnit="False"/>'
              '<Style x:Key="EO_FovSlider" TargetType="Slider" BasedOn="{StaticResource A_Style_Slider_Options}"><Setter Property="Minimum" Value="20"/><Setter Property="Maximum" Value="120"/></Style>\r\n'
              "        </ResourceDictionary>\r\n    </Page.Resources>")
    eo = once(eo, 'Width="806" Height="110"', 'Width="806" Height="275"')
    eo = once(eo, 'Height="100" Width="806" Template="{StaticResource A_ControlTemplate_DescriptionPanelContent}"',
              'Height="110" Width="806" Template="{StaticResource A_ControlTemplate_DescriptionPanelContent}"')
    eo = once(eo, "\r\n                                        </local:CommonListBox>",
              ROW_INDENT + '<Slider Value="{Binding FovChase}" Tag="{StaticResource EO_FovChase}" Style="{StaticResource EO_FovSlider}"/>'
              + ROW_INDENT + '<local:SpinnerControl x:Name="EO_RealtimeShadows" Label="Realtime Shadows" ItemsSource="{Binding ToggleChoices}" Style="{StaticResource A_Style_SpinnerControl_Default}" SelectedIndex="{Binding RealtimeShadows}" Tag="{StaticResource EO_RealtimeShadows}"/>'
              + "\r\n                                        </local:CommonListBox>")
    variants = {"A_VideoOptions_eo_sim": eo}

    other = once(video, "        </ResourceDictionary>\r\n    </Page.Resources>",
                 '            <Style x:Key="Other_Style" TargetType="Slider"><Style.Resources><ResourceDictionary/></Style.Resources></Style>\r\n'
                 '            <local:TileMenuDataItem x:Key="Other_Item" Heading="Other Mod" Description="Another mod."/>\r\n'
                 "        </ResourceDictionary>\r\n    </Page.Resources>")
    other = once(other, 'Width="806" Height="110"', 'Width="806" Height="165"')
    other = once(other, "\r\n                                        </local:CommonListBox>",
                 ROW_INDENT + '<local:SpinnerControl Tag="{StaticResource Other_Item}" Style="{StaticResource A_Style_SpinnerControl_Default}" Label="Other Mod" SelectedIndex="{Binding OtherMod}" ItemsSource="{Binding OnOffOptions}"/>'
                 + "\r\n                                        </local:CommonListBox>")
    variants["A_VideoOptions_other"] = other

    hud = open(pages["A_HUDOptions"], "rb").read().decode()
    hud = once(hud, "                                </local:CommonListBox>",
               '                                        <local:SpinnerControl Label="Platform Option" SelectedIndex="{Binding PlatformOption}" ItemsSource="{Binding OnOffOptions}" Style="{StaticResource A_Style_SpinnerControl_Default}"/>\r\n'
               "                                </local:CommonListBox>")
    variants["A_HUDOptions_platform"] = hud
    for name, text in variants.items():
        pages[name] = os.path.join(WORK, name + ".xaml")
        open(pages[name], "wb").write(text.encode())

    exe = os.path.join(HERE, "offline_test.exe")
    game = os.path.join(MOUNT, "forza_durango_release_final.exe")
    inputs = [pages[n] for n in ("A_VideoOptions_eo_sim", "A_VideoOptions_other", "A_HUDOptions",
                                 "A_HUDOptions_platform", "A_AudioOptions", "A_PauseOptions")]
    run = subprocess.run([exe, game, pages["A_VideoOptions"], WORK] + inputs)
    failed = run.returncode != 0

    defined = set()
    for n in ui.namelist():
        if n.endswith(".xaml"):
            defined |= set(re.findall(rb'x:Key="([^"]+)"', ui.read(n)))
    expected_height = {"A_VideoOptions": "275", "A_VideoOptions_eo_sim": "275", "A_VideoOptions_other": "165"}
    for name, height in expected_height.items():
        data = open(os.path.join(WORK, name + ".timelock.xaml"), "rb").read()
        root = ET.fromstring(data)
        keys = [e.get(X + "Key") for e in root.find(f"{W}Page.Resources/{W}ResourceDictionary")]
        lists = [e for e in root.iter(L + "CommonListBox") if e.get(X + "Name") == "OptionsList"]
        rows = [c.get("SelectedIndex") or c.get("Value") for c in lists[0]] if len(lists) == 1 else []
        missing = sorted(k.decode() for k in set(re.findall(rb"StaticResource ([A-Za-z0-9_]+)", data))
                         if k not in defined and not re.match(rb"(TimeLock|EO|Other)_", k))
        ok = (keys[-5:] == ["TimeLock_Item_Enabled", "TimeLock_Item_Hour", "TimeLock_Item_Weather", "TimeLock_HourFormat", "TimeLock_HourSlider"]
              and rows[:2] == ["{Binding Gamma}", "{Binding Subtitles}"]
              and rows[-3:] == ["{Binding TimeLockEnabled}", "{Binding TimeLockHour, Mode=TwoWay}", "{Binding TimeLockWeather}"]
              and lists[0][-1].get("ItemsSource") == "{Binding TimeLockWeatherOptions}"
              and lists[0].get("Height") == height and not missing)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: well-formed; {len(rows)} rows (Brightness, Subtitles first, ours last); "
              f"list Height {lists[0].get('Height') if lists else '?'}; missing keys {missing}")
        failed |= not ok
    for name in ("A_HUDOptions", "A_HUDOptions_platform"):
        ok = not os.path.exists(os.path.join(WORK, name + ".timelock.xaml"))
        print(f"  {'ok  ' if ok else 'FAIL'} {name}: not edited")
        failed |= not ok
    print("\nALL OFFLINE TESTS PASSED" if not failed else "\nSOME TESTS FAILED")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
