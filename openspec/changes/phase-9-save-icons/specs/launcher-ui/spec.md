## MODIFIED Requirements

### Requirement: Save info
At boot the UI SHALL list the root directories of both memory cards once. A game's saves are the directories whose
name contains its serial (for example `BASLUS-20946...` for `SLUS-20946`). The header SHALL show:
- the count and the card ("2 saves en Memory Card 1") and the newest modification date;
- "Sin saves" when there are none;
- "Sin memory card" when no card is inserted.

The card's icon slot SHALL show the newest save's 3D icon once it is loaded (save-icons spec), a memory-card glyph
until then, and an outline glyph when the game has no saves.

#### Scenario: No saves
- **WHEN** the selected game has no directory on either card
- **THEN** the header says "Sin saves"

#### Scenario: Icon in the card
- **WHEN** the selected game has a save and its icon has loaded
- **THEN** the card's icon slot shows the 3D icon instead of the glyph
