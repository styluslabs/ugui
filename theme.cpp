// SVG GUI (default) CSS style

const char* defaultStyleCSS = R"#(
svg.window  /* :root */
{
  --dark: #101010;
  --window: #303030;
  --light: #505050;
  --base: #202020;
  --button: #555555;
  --hovered: #32809C;
  --pressed: #32809C;
  --checked: #0000C0;
  --title: #2EA3CF;
  --text: #F2F2F2;
  --text-weak: #A0A0A0;
  --text-bg: #000000;
  --icon: #CDCDCD;
  --icon-disabled: #808080;
}

/* light theme */
svg.window.light
{
  --dark: #F0F0F0;
  --window: #DDDDDD;
  --light: #CCCCCC;
  --base: #FFFFFF;
  --button: #D0D0D0;
  --hovered: #B8D8F9;
  --pressed: #B8D8F9;
  --checked: #A2CAEF;
  --title: #2EA3CF;
  --text: #000000;
  --text-weak: #606060;
  --text-bg: #F2F2F2;
  --icon: #303030;
  --icon-disabled: #A0A0A0;
}

.menu { fill: var(--light); }
.menuitem { fill: var(--window); }
.menuitem.hovered { fill: var(--hovered); }
.menuitem.checked { fill: var(--checked); }
.cbmenuitem.checked { fill: var(--window); }
.cbmenuitem.hovered { fill: var(--hovered); }
.menuitem.pressed { fill: var(--pressed); }
.menuitem.disabled { fill: var(--light); }

.menu-icon-container { display: block; }
.no-icon-menu .menu-icon-container { display: none; }

.list { fill: var(--base); }
.listitem { fill: var(--base); }
.listitem.pressed { fill: var(--pressed); }

/* stroked paths in icons use stroke="currentColor" for now */
.icon { fill: var(--icon); color: var(--icon); }
.disabled .icon { fill: var(--icon-disabled); color: var(--icon-disabled); }

/* set default font-size at top-level instead of every <text> */
.window { font-size: 15; }

text { fill: var(--text); }
text.window-title { fill: var(--title); font-size: 18; margin: 15px 0; }  /* link */
text.weak { fill: var(--text-weak); }
text.negative { fill: var(--text-bg); }  /* for inverted background - light in this case */
text.disabled { fill: var(--light); }

/* TODO: combine toolbutton and menuitem styles */
.toolbar { fill: var(--dark); }
.toolbar.graybar { fill: var(--light); }
.toolbar.statusbar .toolbar-bg { fill-opacity: 0.75; }
.toolbutton { fill: none; }
.toolbutton.hovered { fill: var(--hovered); }
.toolbutton.pressed { fill: var(--pressed); }
.toolbutton.checked .checkmark { fill: #0080FF; }  /* highlight ... was #0000C0 */

.toolbutton.checked.once .checkmark { fill: #0060C0; }  /* for non-sticky; random color between pressed and checked */

.separator { fill: var(--light); }  /* toolbar (and menu?) separator */
.graybar .separator { fill: #808080; }
.statusbar .separator { fill: #808080; }

.statusbar { font-size: 24; }  /* to go with 50% scaling hack */

.warning { fill: #FFFF00; }
.warning text { fill: #000000; }

.pushbutton { fill: var(--button); }
.pushbutton.pressed { fill: var(--pressed); }
.pushbutton.disabled { fill: #404040; }
.pushbutton.checked { fill: var(--checked); }

.button-container .pushbutton { margin: 0 4; }

/* for color and width preview buttons with stroked borders */
.previewbtn { color: #808080; }
.previewbtn.hovered { color: var(--hovered); }
.previewbtn.pressed { color: var(--pressed); }

/* combobox, textbox, spinbox */
.inputbox { fill: var(--base); }
.comboitem { fill: var(--base); }  /* #181818 */
.comboitem.hovered { fill: var(--hovered); }
.comboitem.pressed { fill: var(--pressed); }
.inputbox.disabled text { fill: var(--light); }
.disabled .inputbox text { fill: var(--light); }

.checkbox { color: var(--icon); }
.checkmark { color: #33B3E3; }
.checkbox .checkmark { display: none; }
.checkbox.checked .checkmark { display: block; }
.menuitem.checked .checkbox .checkmark { display: block; }

.slider-handle-outer { fill: grey; }
.slider-handle-inner { fill: black; }

.scroll-handle { fill: var(--title); }

.inputbox-bg { stroke: var(--base); stroke-width: 2; }
/* previously we used .focused .inputbox-bg, but this created an issue w/ widgets inside a scroll widget */
.focused > .inputbox-bg { stroke: var(--icon); }
.text-cursor { fill: var(--icon); }
tspan.text-selection { fill: var(--text-bg); }
.text-selection-bg { fill: var(--text); }
/* margin of .textbox-container must match stroke-width of .inputbox-bg */
.textbox-container { margin: 0 2; }

.hrule { fill: grey; }
.hrule.title { fill: var(--title); }
.hrule.title.inactive { fill: #D8D8D8; }

/* display node isn't in style, so SVG attribute just gets overwritten and can't be restored! */
.window-overlay { fill: black; fill-opacity: 0.5; display: none; }
.disabled .window-overlay { display: block; }

.dialog { fill: var(--window); }  /* .dialog-bg doesn't work for scroll view inside dialog! */
.panel-header { fill: var(--window); }
.panel-header .toolbar { fill: var(--window); }

.splitter { fill: var(--dark); }

rect.background { shape-rendering: crispEdges; }
/*rect.inputbox-bg { shape-rendering: crispEdges; }  -- problem because this applies to stroke too */

.menu, .dialog { box-shadow: 0px 0px 10px 0px rgba(0,0,0,0.5); }
/*.menu, .dialog { box-shadow: 0px 0px 40px 0px rgba(0,0,0,0.40); }*/ /* like android, but doesn't look great on computer */
/*.menu, .dialog { box-shadow: 6px 6px 4px -4px rgba(0,0,0,0.375); }*/ /* offset shadow like Windows */
)#";

// document containing prototypes for widgets; identified by SVG class
static const char* defaultWidgetSVG = R"#(
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">

  <g id="menu" class="menu" display="none" position="absolute" box-anchor="fill" layout="box">
    <rect class="background" box-anchor="fill" width="20" height="20"/>
    <g class="child-container" box-anchor="fill" layout="flex" flex-direction="column">
    </g>
  </g>

  <!-- thin space between menu items acts as separator, so we don't want class=background on menu item BG -->
  <g id="menuitem-standard" class="menuitem" box-anchor="fill" margin="1 0" layout="box">
    <rect box-anchor="hfill" width="150" height="36"/>
    <g box-anchor="left vfill" layout="flex" flex-direction="row">
      <g class="menu-icon-container" layout="box" margin="0 2">
        <!-- invisible rectangle is to fix size even if no icon -->
        <rect fill="none" width="36" height="36"/>
        <use display="none" class="icon" height="36" xlink:href=""/>
      </g>
      <text class="title" margin="0 12"></text>
    </g>
  </g>

  <g id="menuitem-custom" class="menuitem" box-anchor="fill" margin="1 0" layout="box">
    <rect box-anchor="hfill" width="150" height="36"/>
    <g class="menuitem-container" box-anchor="fill" layout="box">
    </g>
  </g>

  <g id="menu-separator" class="menu-separator" layout="box">
    <rect box-anchor="hfill" width="36" height="9"/>
    <rect class="separator" box-anchor="hfill" margin="0 4" width="26" height="6"/>
  </g>

  <g id="toolbar" class="toolbar" box-anchor="hfill" layout="box">
    <rect class="toolbar-bg background" box-anchor="fill" width="20" height="20"/>
    <g class="child-container" box-anchor="hfill" layout="flex" flex-direction="row">
    </g>
  </g>

  <g id="vert-toolbar" class="toolbar vert-toolbar" box-anchor="vfill" layout="box">
    <rect class="toolbar-bg background" box-anchor="fill" width="20" height="20"/>
    <g class="child-container" box-anchor="vfill" layout="flex" flex-direction="column">
    </g>
  </g>

  <!-- note that class=checkmark can be moved to the filling rect to color whole background when checked -->
  <g id="toolbutton" class="toolbutton" layout="box">
    <rect class="background" box-anchor="hfill" width="36" height="42"/>
    <rect class="checkmark" box-anchor="bottom hfill" margin="0 2" fill="none" width="36" height="3"/>
    <g margin="0 3" box-anchor="fill" layout="flex" flex-direction="row">
      <use class="icon" height="36" xlink:href="" />
      <text class="title" display="none" margin="0 9"></text>
    </g>
  </g>

  <g id="toolbar-separator" class="toolbar-separator" box-anchor="vfill" layout="box">
    <rect fill="none" box-anchor="vfill" width="16" height="36"/>
    <rect class="separator" box-anchor="vfill" margin="4 0" width="2" height="36"/>
  </g>

  <g id="vert-toolbar-separator" class="toolbar-separator" box-anchor="hfill" layout="box">
    <rect fill="none" box-anchor="hfill" width="36" height="16"/>
    <rect class="separator" box-anchor="hfill" margin="0 4" width="36" height="2"/>
  </g>

  <g id="pushbutton" class="pushbutton" box-anchor="fill" layout="box">
    <rect class="background"  box-anchor="hfill" width="36" height="36"/>  <!-- rx="8" ry="8" -->
    <text class="title" margin="8 8"></text>
  </g>

  <g id="radiobutton" class="radiobutton checkbox">
    <rect fill="none" width="26" height="26"/>
    <circle fill="none" stroke="currentColor" stroke-width="1.5" cx="13" cy="13" r="8" />
    <circle class="checkmark" fill="currentColor" cx="13" cy="13" r="5" />
  </g>

  <g id="checkbox" class="checkbox">
    <rect fill="none" width="26" height="26"/>
    <rect x="4" y="4" fill="none" stroke="currentColor" stroke-width="1" width="18" height="18"/>
    <polygon class="checkmark" fill="currentColor"
        points="25.38,4.92,23.18,2.72,12.72,13.29,8.53,9.12,6.33,11.32,12.73,17.69,13.85,16.57,13.84,16.56"/>
  </g>

  <!-- non-editable textbox for combobox and spinbox -->
  <g id="textbox" class="textbox" box-anchor="fill" layout="box">
    <text box-anchor="left" margin="3 6"></text>
  </g>

  <g id="combobox" class="inputbox combobox" layout="box">
    <rect class="min-width-rect" width="150" height="36" fill="none"/>
    <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

    <g class="combo_content" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
      <g class="textbox combo_text" box-anchor="fill" layout="box">
      </g>

      <g class="combo_open" box-anchor="vfill" layout="box">
        <rect fill="none" box-anchor="vfill" width="28" height="28"/>
        <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_down.svg" />
      </g>
    </g>

    <g class="combo_menu menu" display="none" position="absolute" top="100%" left="0" box-anchor="fill" layout="box">
      <!-- invisible rect to set minimum width; proper soln would be to support left=0 right=0 to stretch -->
      <rect class="combo-menu-min-width" width="150" height="36" fill="none"/>
      <rect class="background" box-anchor="fill" width="20" height="20"/>
      <g class="child-container" box-anchor="fill" layout="flex" flex-direction="column">
        <g class="combo_proto comboitem" display="none" box-anchor="fill" layout="box">
          <rect box-anchor="hfill" width="36" height="36"/>
          <text box-anchor="left" margin="8 8">Prototype</text>
        </g>
      </g>
    </g>
  </g>

  <g id="spinbox" class="inputbox spinbox" layout="box">
    <rect class="min-width-rect" width="150" height="36" fill="none"/>
    <rect class="inputbox-bg" box-anchor="fill" width="150" height="36"/>

    <g class="spinbox_content" box-anchor="fill" layout="flex" flex-direction="row" margin="0 2">
      <g class="textbox spinbox_text" box-anchor="fill" layout="box">
      </g>

      <!-- inc/dec buttons -->
      <g class="toolbutton spinbox_dec" box-anchor="vfill" layout="box">
        <rect class="background" width="28" height="28"/>
        <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_left.svg" />
      </g>
      <g class="toolbutton spinbox_inc" box-anchor="vfill" layout="box">
        <rect class="background" width="28" height="28"/>
        <use class="icon" width="28" height="28" xlink:href=":/icons/chevron_right.svg" />
      </g>
    </g>
  </g>

  <g id="slider" class="slider" box-anchor="hfill" layout="box">
    <rect class="slider-bg background" box-anchor="hfill" width="200" height="48"/>
    <g class="slider-handle">
      <rect class="slider-handle-outer" x="-12" y="-2" width="24" height="52"/>
      <rect class="slider-handle-inner" x="-9" y="0" width="18" height="48"/>
    </g>
  </g>

  <svg id="dialog" class="window dialog" display="none" layout="box">
    <rect class="dialog-bg background" box-anchor="fill" width="20" height="20"/>
    <g class="dialog-layout" box-anchor="fill" layout="flex" flex-direction="column">
      <text class="window-title title" box-anchor="left"></text>
      <rect class="hrule title" box-anchor="hfill" width="20" height="2"/>
      <g class="body-container" box-anchor="fill" layout="flex" flex-direction="column">
      </g>
      <g class="button-container dialog-buttons" margin="5 4" box-anchor="hfill" layout="flex" flex-direction="row">
      </g>
    </g>
  </svg>

  <rect id="scroll-handle" class="scroll-handle" box-anchor="vfill" width="4" height="20" rx="2" ry="2"/>

</svg>
)#";
