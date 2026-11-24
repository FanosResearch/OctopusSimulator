#ifndef ADDRESS_MAPPING_H
#define ADDRESS_MAPPING_H

#include <vector>
#include <map>
#include <iostream>
#include "Request.h"

namespace MCsim
{
	class AddressMapping
	{
	public:
		AddressMapping(const std::string &format, const unsigned int (&memTable)[6]);
		void addressMapping(Request *request);
		unsigned int lowestBitsValue(unsigned int bank, unsigned int bits);
		// Octopus: where `field` (memOrg) sits in the line address (byte address >> 6): its lowest
		// bit and its width, from the AddressMapping string and the device geometry. False when
		// the mapping does not name the field.
		bool fieldBits(unsigned field, unsigned &lo, unsigned &width) const;

	private:
		std::vector<std::pair<unsigned int, unsigned int>> decodeIndex;
		unsigned log2(unsigned num_value)
		{
			unsigned logbase2 = 0;
			unsigned value = num_value;
			unsigned orig = value;
			value >>= 1;
			while (value > 0)
			{
				value >>= 1;
				logbase2++;
			}
			if ((unsigned)1 << logbase2 < orig)
				logbase2++;
			return logbase2;
		}
	};
} // namespace MCsim

#endif /* ADDRESS_MAPPING_H */
