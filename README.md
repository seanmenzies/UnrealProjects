# UnrealProjects

The memory component is an actor component attached to all custom characters in the game. It acts as a respository for and processor of data about the world's various actors. Characters interface with other memory components to stay updated on changes to health, weapons, armour and other "visible" aspects of the world.

The damage component is responsible for dealing and receiving damage. It interfaces with the character's inventory and stats components to factor in aspects such as arrow fall-off, character buffs and debuffs, character skill, weapon stats etc. On the receiving end, it compares the incoming damage with armour and other types of resistance. Fully networked, the system interfaces with in-engine AnimNotifyStates to calculate frame-independent traces.

The AI controller class manages such as aspects as perception (vision and hearing), path following particulars, target selection and combat decisions.
