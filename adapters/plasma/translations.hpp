#pragma once

namespace nekomote::plasma
{
// Called during QML module import on the GUI thread, before either package page
// is instantiated. Uses the desktop locale, independently of the native panel.
void install_translations();
} // namespace nekomote::plasma
