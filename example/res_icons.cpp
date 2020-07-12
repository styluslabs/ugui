// Based on my reading of the spec, the width and height from <use> replace the width and height on <svg>, so
//  viewBox is needed to scale the contents to the width and height from <use> instead of clipping.

static const char* icons__checkbox_check_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g>
  <rect display="none" x="14.5" y="14.5" fill="none" stroke="#cccccc" stroke-width="4" width="67" height="67"/>
  <polygon fill="#33B3E3" points="94,18.231 85.857,10.086 47.121,49.207 31.59,33.793 23.445,41.94 47.152,65.5 51.297,61.355 51.277,61.338"/>
</g>
</svg>
)~~~~";

static const char* icons__checkbox_nocheck_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g>
  <rect x="14.5" y="14.5" fill="none" stroke="#cccccc" stroke-width="4" width="67" height="67"/>
</g>
</svg>
)~~~~";

static const char* icons__chevron_down_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g>
  <polygon points="72.628,32.223 48.002,53.488 23.366,32.223 18.278,38.121 48.002,63.777 77.722,38.121 "/>
</g>
</svg>
)~~~~";

static const char* icons__chevron_left_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g>
  <polygon points="57.879,18.277 32.223,47.998 57.879,77.723 63.776,72.634 42.512,47.998 63.776,23.372"/>
</g>
</svg>
)~~~~";

static const char* icons__chevron_right_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g>
  <polygon points="32.223,23.372 53.488,47.998 32.223,72.634 38.121,77.723 63.777,47.998 38.121,18.277"/>
</g>
</svg>
)~~~~";

static const char* icons__ic_menu_overflow_svg = R"~~~~(<svg width="96px" height="96px" viewBox="0 0 96 96">
<g class="icon">
  <rect x="40.586" y="63.077" width="14.986" height="14.996"/>
  <rect x="40.586" y="40.084" width="14.986" height="14.996"/>
  <rect x="40.586" y="18.091" width="14.986" height="14.995"/>
</g>
</svg>
)~~~~";

static void LOAD_RES_FN() { addStringResources({
  {"icons/checkbox_check.svg", icons__checkbox_check_svg},
  {"icons/checkbox_nocheck.svg", icons__checkbox_nocheck_svg},
  {"icons/chevron_down.svg", icons__chevron_down_svg},
  {"icons/chevron_left.svg", icons__chevron_left_svg},
  {"icons/chevron_right.svg", icons__chevron_right_svg},
  {"icons/ic_menu_overflow.svg", icons__ic_menu_overflow_svg},
}); }
#undef LOAD_RES_FN
