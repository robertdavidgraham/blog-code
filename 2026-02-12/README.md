# Pi π is 3.141592653589793

This blogpost is about how *pi* or *π* is precisely
3.141592653589793 as far as most modern calculations
are concerned. That's becuase modern number crunching
uses 64-bit IEEE 754 floating point numbers. This is the
number of **significant digits** that can be held in such
floating point numbers.

Like many such topics, there are Holy Wars about this, as
techies angrily make some other claim.

The angriest nerds claim that the value of a 64-bit float
holding pi *π* is *actually*:

```
3.141592653589793115997963468544185161590576171875
```

This is false. The issue is **significant digits**.
A mantissa with 52 binary digits cannot possibly hold
a number with 49 decimal digits. The maximum signficant
decimal digits is 15 to 17.

It's 100 miles between Nashville and Huntsville. Translated
to metric, it would be absurd to say that it's 160.9344 kilometers.
A nerd might justify this by claiming this is the exact conversion
factor. That's how the modern imperial "mile" is defined, as
precisely 1.609344 km, according to an international agreeement
from the year 1959, and through numerous government documents.

But that's still silly, because the original number doesn't have 7
significant digits. It as 2, maybe 1. At three significant digits,
the distance is 103 miles and 165 km.

By insisting on 160.9344 km, you are **hallucinating** more
significant digits, pretending the original number was 100.0000
and not simply 100.

Well, yes, this is something we often do for integers and constants.
The conversion factor here has **infinite** signicant digits,
`1.60934400000...`. When we see integer constants, we likewise
assume they have infinite precision, an infinite number of zeroes
appended, like `100.00000...`. But that's an assumption, and it
wouldn't be correct when talking about a measurement of the 
distance between two cities.

As you know, a *floating-point* number is composed from a
*mantissa* and *exponent*. This creates two integers that
we can then divide.

That's what the code with this project, `simple-div.c` does.
It extracts the two two integers and divides them, manually,
printing the result to the command-line:

```
$ gcc -Wall -Wextra -Wpedantic simple-div.c
$ ./a.out 3.141592653589793
Sign: 0, Exponent: 1, Mantissa: 0x921fb54442d18
Fraction: 7074237752028440 / 2251799813685248
3.141592653589793115997963468544185161590576171875
```

The 52 bit mantissa translates to the integer `7074237752028440`
and the exponent expands to `2251799813685248`. When dividing
these two integers, you get that long expansion with 49 digits.

But, while the exponenet (denominator) of the fraction has
infinite precision, the mantissa (nominator) does not. It
represents the precise value of `7074237752028440` and not
`7074237752028440.000000...`.

Doing that long expansion **hallucinates** extra zeroes.

The point where this hallucination happens is on line #14:

```c
        remainder *= 10;
```

Each time you do this, you add another 0 as a significant digit.

A 64-bit mantissa is a 52 bit integer, and only 52 bits. It does
not represent a larger number padded with an infinite number of
zeroes afterwards. Yes, yes, you assume any integer also represents
a padding of infinite zeroes, but in this calculation, it
does not, no more than saying "100 miles between two cities" 
represents a precise number of `100.000...`.

Most programming languages will happily print the excess,
non-significant digits. But not all programming languages. One
exception is JavaScript, which intelligently prints only
the **significant digits**.

You can see that in the `Node.js` session below:

```
$ node
Welcome to Node.js v23.7.0.
Type ".help" for more information.
> 3.141592653589793115997963468544185161590576171875
3.141592653589793
> 3.141592653589793
3.141592653589793
> 3.14159265358979311111111111111111111111
3.141592653589793
> 
```

What JavaScript does is print the *minimum digits* necessary to
represent a number. It's actually a complex task. The **Dragon4**
algorithm is the most popular, though with some variations to
make it faster. It's what JavaScript, NumPy, and other systems
use.

It's the **correct** answer of the information contained in
a floating point number without including **hallucinated** digits.

In my code above, you could simply put `15` as the `max_digits`
parameter to reflect the proper number of digits.

```
$ ./a.out 3.141592653589793 15
Sign: 0, Exponent: 1, Mantissa: 0x921fb54442d18
Fraction: 7074237752028440 / 2251799813685248
3.141592653589793
```

But that's only a guess at the number of significant digits.
The correct number is usually `16`, not `15`.

There is a great
 (page)[https://marekknapek.github.io/double/#?n=0x400921fb54442d18]
for bit fiddling floating point numbers were we can see what
the neighboring numbers look like, plus one bit or minus one
bit.

```
$ node
Welcome to Node.js v23.7.0.
Type ".help" for more information.
> 3.141592653589792671908753618481568992137908935546875
3.1415926535897927
> 3.141592653589793115997963468544185161590576171875
3.141592653589793
> 3.141592653589793560087173318606801331043243408203125
3.1415926535897936
> 
```

As you can see, sometimes there's 15 significant digits, and
sometimes 16 significant digits. You can't blindly print
a 64-bit float and guess at the number of significant digits.


Some other links:
- (https://fabiensanglard.net/floating_point_visually_explained/)[https://fabiensanglard.net/floating_point_visually_explained/]






