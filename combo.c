// Button combos from config.ini (phase 12): "L1+L2+R1+R2+START+SELECT" -> libpad mask (PAD_* bits, as the game's own
// pad data, so the IGR in the ORBIT Neutrino fork compares it directly). Spanish and English names, any case.
#include <string.h>
#include <strings.h>
#include "combo.h"

static const struct { const char *name; unsigned short bit; } names[] = {
	{"SELECT", 0x0001}, {"L3", 0x0002}, {"R3", 0x0004}, {"START", 0x0008}, {"UP", 0x0010}, {"ARRIBA", 0x0010},
	{"RIGHT", 0x0020}, {"DERECHA", 0x0020}, {"DOWN", 0x0040}, {"ABAJO", 0x0040}, {"LEFT", 0x0080},
	{"IZQUIERDA", 0x0080}, {"L2", 0x0100}, {"R2", 0x0200}, {"L1", 0x0400}, {"R1", 0x0800}, {"TRIANGLE", 0x1000},
	{"TRIANGULO", 0x1000}, {"CIRCLE", 0x2000}, {"CIRCULO", 0x2000}, {"X", 0x4000}, {"CROSS", 0x4000},
	{"SQUARE", 0x8000}, {"CUADRADO", 0x8000}};

unsigned combo_mask(const char *s)
{
	unsigned mask = 0;
	while (*s) {
		while (*s == ' ' || *s == '+') s++;
		int n = strcspn(s, "+ ");
		if (!n) break;
		unsigned bit = 0;
		for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
			if ((int)strlen(names[i].name) == n && !strncasecmp(s, names[i].name, n)) bit = names[i].bit;
		if (!bit) return 0; // an unknown name disables the combo rather than guess
		mask |= bit;
		s += n;
	}
	return mask;
}

#ifdef SELFTEST // host check: `make test`
#include <assert.h>
#include <stdio.h>
int main(void)
{
	assert(combo_mask("L1+L2+R1+R2+START+SELECT") == 0x0F09); // OPL's exit combo: 0xF0 / 0xF6 active-low bytes
	assert(combo_mask("l1 + l2 + r1 + r2 + l3 + r3") == 0x0F06); // OPL's power-off combo, spaces and case
	assert(combo_mask("TRIANGULO+Cuadrado+x") == 0xD000);
	assert(combo_mask("L1+PATADA") == 0 && combo_mask("") == 0);
	puts("combo selftest ok");
	return 0;
}
#endif
