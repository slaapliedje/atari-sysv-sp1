/*
 * asvrt.c - GCC for an a.out target (the mint GCC) calls __main at the top
 * of main() to run constructors; there are none.
 */
void __main(void)
{
}
