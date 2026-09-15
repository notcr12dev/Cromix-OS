#ifndef EDIT_H
#define EDIT_H

/* Cronix OS · `edit`: nano-style in-kernel text editor.
 * Arrows move, type to insert, ^O saves, ^X quits.
 * 32KB buffer cap. No syntax highlight (yet). */
void edit_file(const char *path); /* path like /home/notes.txt */

#endif /* EDIT_H */
