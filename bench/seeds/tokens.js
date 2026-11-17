// Tokens of every kind in dense runs: names, operators, numbers, strings
// with escapes, templates, regular expressions and comments.
var re = /ab+c/gi, re2 = /[a-z\/]+\d{2,3}/u, t = `x${a + b}y${`z${c}`}w`;
var n = 0x1F + 0b101 - 0o17 * 1_000_000 / 3.14e-2 % 7 ** 2 + .5 + 5. + 10n;
var s = 'it\'s a \x41B\u{1F600} string\n' + "tab\there" + 'line\
continued';
for (let i = 0, j = n; i < j && !(i >= 10) || i !== j; i++, --j) {
  a[i] = b[j] ?? c?.d?.[e](f) ?? (g || h && k);
  x <<= 1; y >>>= 2; z ||= w; q &&= r; p ??= o; m **= 2; l |= 1; u ^= 3;
}
if (typeof n === 'number' && n instanceof Number || !(n in obj)) { throw new Error(`bad ${n}`); }
const fn = async (a, b = 2, ...rest) => { await a; yield* b; return a => b => a + b; };
class K extends Base { #p = 1; static #q; get v() { return this.#p; } static { K.#q = 2; } }
label: while (true) { switch (n) { case 1: break label; default: continue label; } }
// a trailing comment with some text in it to scan over
/* and a block comment
   spanning two lines */ var done = true;
