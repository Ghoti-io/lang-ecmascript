// A small ASCII script: identifiers, numbers, strings, a few lines.
function fib(n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
var total = 0;
for (var i = 0; i < 20; i++) {
  total += fib(i) * 3 + (i & 7);
}
var greeting = "hello, world";
console.log(greeting.length, total);
