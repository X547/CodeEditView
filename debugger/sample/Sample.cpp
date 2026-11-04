// A program to try the debugger on: threads, nested calls, structures.
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include <string>
#include <vector>


struct Point {
	int x;
	int y;
};


struct Shape {
	std::string name;
	std::vector<Point> points;
};


static int Area(const Shape &shape)
{
	int sum = 0;
	for (size_t i = 0; i < shape.points.size(); i++) {
		const Point &a = shape.points[i];
		const Point &b = shape.points[(i + 1) % shape.points.size()];
		sum += a.x * b.y - b.x * a.y;
	}
	return sum / 2;
}


static void *Worker(void *data)
{
	int *counter = static_cast<int*>(data);
	for (;;) {
		(*counter)++;
		usleep(200000);
	}
	return nullptr;
}


int main(int argc, char **argv)
{
	int counter = 0;
	pthread_t thread;
	pthread_create(&thread, nullptr, Worker, &counter);

	Shape square {"square", {{0, 0}, {4, 0}, {4, 4}, {0, 4}}};
	printf("%s: %d arguments\n", argv[0], argc - 1);
	for (int step = 0; ; step++) {
		square.points[2].x++;
		const int area = Area(square);
		printf("step %d: area %d, counter %d\n", step, area, counter);
		fflush(stdout);
		sleep(1);
	}
	return 0;
}
