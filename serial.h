#ifndef _SERIAL_H
#define _SERIAL_H

#include <string>

namespace cee {
	namespace monitor {
		class Serial {
			public:
				Serial(const std::string& filename);
				virtual ~Serial();

			private:
				void Panic(int errc);

			private:
				int m_Fd = 0;
		};
	}
}

#endif

