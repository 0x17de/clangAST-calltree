#include <iostream>

int main()
{
	return test();
}

int test() {
	return test2(3) + 2;
}

int test2(int a) {
	// this is complicated
	if (a == 2) {
		return 1;
	} else {
		return 2;
	}
}
