//#include <iostream>

class T {
	void t(int c) {
		return c;
	}
};

int test2(int a) {
	// this is complicated
	if (a == 2) {
		return 1;
	} else {
		return 2;
	}
}

int test() {
	return test2(3) + 2;
}

int main()
{
	test();
	test();
	T t;
	t.t(3);
	return test();
}

