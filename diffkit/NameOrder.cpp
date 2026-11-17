#include "NameOrder.h"

#include <ctype.h>


namespace DiffKit {


bool NameLess(const std::string &a, const std::string &b)
{
	size_t i = 0;
	size_t j = 0;
	while (i < a.size() && j < b.size()) {
		const unsigned char ca = a[i];
		const unsigned char cb = b[j];
		if (isdigit(ca) && isdigit(cb)) {
			size_t endA = i;
			size_t endB = j;
			while (endA < a.size() && isdigit(static_cast<unsigned char>(a[endA]))) {
				endA++;
			}
			while (endB < b.size() && isdigit(static_cast<unsigned char>(b[endB]))) {
				endB++;
			}
			// Without leading zeros, the longer number is the larger.
			size_t startA = i;
			size_t startB = j;
			while (startA + 1 < endA && a[startA] == '0') {
				startA++;
			}
			while (startB + 1 < endB && b[startB] == '0') {
				startB++;
			}
			if (endA - startA != endB - startB) {
				return endA - startA < endB - startB;
			}
			const int order = a.compare(startA, endA - startA, b, startB, endB - startB);
			if (order != 0) {
				return order < 0;
			}
			i = endA;
			j = endB;
			continue;
		}
		const int la = tolower(ca);
		const int lb = tolower(cb);
		if (la != lb) {
			return la < lb;
		}
		i++;
		j++;
	}
	if (a.size() - i != b.size() - j) {
		return a.size() - i < b.size() - j;
	}
	return a < b;
}


} // DiffKit
