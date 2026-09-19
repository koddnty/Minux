void mPrint(const char *format, int len);

int chooseNum(int a, int b) {
    if(a > b) {
        mPrint("a is greater than b\n", 22);
    } else {
        mPrint("b is greater than a\n", 22);
    }
    return 0;
}