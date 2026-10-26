#ifndef FALLOUT_GAME_RT_AUTOTEST_H_
#define FALLOUT_GAME_RT_AUTOTEST_H_

namespace fallout {

// RT: Automated end-to-end test of the real-time prototype.
//
// Enabled with the `--rt-autotest` command line switch. Starts a new game
// with the first premade character, then drives a simulated controller
// through movement, running, targeting, combat, interaction and pausing in
// the opening cave. Results go to logs\autotest.log (plus screenshots).

void rt_autotest_parse_args(int argc, char** argv);
bool rt_autotest_enabled();
void rt_autotest_tick();
bool rt_autotest_finished();
// `--rt-autotest-menus`: the controller menu test (main menu, character
// creation, pause menu) instead of the gameplay test.
bool rt_autotest_menus_mode();

} // namespace fallout

#endif /* FALLOUT_GAME_RT_AUTOTEST_H_ */
