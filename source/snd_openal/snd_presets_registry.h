#ifndef QFUSION_SND_PRESETS_HOLDER_H
#define QFUSION_SND_PRESETS_HOLDER_H

#include <stdint.h>

struct EfxPresetEntry;
class ReverbEffect;
class EaxReverbEffect;

/**
 * An instance of this class allows retrieval of presets by a preset name.
 */
class EfxPresetsRegistry {
	friend struct EfxPresetEntry;

	EfxPresetEntry *hashBins[97];

	void Register( EfxPresetEntry *entry );

	EfxPresetsRegistry();

	static EfxPresetsRegistry instance;
public:
	static const EfxPresetsRegistry *Instance() { return &instance; }

	/**
	 * Tries to retrieve a preset by name.
	 * A name should correspond to some of predefined presets from {@code efx-presets.h}.
	 * The common presets suffix must be omitted. Letters case does not matter.
	 * @param name a name of a requested preset
	 * @return a reference to a preset if it has been found, null otherwise
	 */
	const EfxPresetEntry *FindByName( const char *name ) const;
};


#endif
