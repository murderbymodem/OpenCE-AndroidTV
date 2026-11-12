/*
OBJECT_SHADOWS.C

display.object_shadows: whether the objects' shadows are drawn
(render_objects.c, render_object_shadows). Each object that casts one is
drawn into the shadow map, the map blurred and then projected onto the
ground, each step on a render target of its own; on GPUs that draw in tiles
each change of target closes a render pass, which costs about 130 us of the
CPU's time a change on a Mali-G310 (the 32-bit Android app's
debug.gl_profile), some 7 ms of a frame with a game's players in view.
Stock (true) draws them.
*/

/* port/linux/src/port_config.c */
int config_boolean(const char *name);
unsigned long config_changes(void);

int object_shadows_wanted(
	void)
{
	static unsigned long changes = (unsigned long)-1;
	static int wanted = 1;

	if (changes != config_changes())
	{
		changes = config_changes();
		wanted = config_boolean("display.object_shadows") != 0;
	}
	return wanted;
}
