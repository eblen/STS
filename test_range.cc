#include <stdio.h>

#include "range.h"

Range<Ratio> f(Range<Ratio> r) { return r; }

int main() {
    Range<int> i(2,10);
    Range<Ratio> s (Ratio(1,2));
    Range<int> r = i.subset(s);
    printf("%d %d\n", r.start, r.end);
    //Range<Ratio> t (0,{2,3});
    r = f({0,{2,3}})*3;
    printf("%d %d\n", r.start, r.end);
}
