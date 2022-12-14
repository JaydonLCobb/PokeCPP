#include <unistd.h>
#include <ncurses.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <vector>

#include "io.h"
#include "character.h"
#include "poke327.h"

typedef struct io_message {
    /* Will print " --more-- " at end of line when another message follows. *
     * Leave 10 extra spaces for that.                                      */
    char msg[71];
    struct io_message *next;
} io_message_t;

static io_message_t *io_head, *io_tail;


bool flee = false;

class trainer_pokemon {
public:
    string name, gender, shiny, move_one, move_two;
    int level, hp, attack, defense, sp_attack, sp_defense, speed, cur_hp, move_one_id, move_two_id, base_speed, poke_id;
    trainer_pokemon(string name, string gender, string shiny, string move_one, string move_two, int level, vector<int> stats, int move_one_id, int move_two_id, int base_speed, int poke_id) {
        this->name = name;
        this->gender = gender;
        this->shiny = shiny;
        this->move_one = move_one;
        this->move_two = move_two;
        this->level = level;
        hp = stats[0];
        attack = stats[1];
        defense = stats[2];
        sp_attack = stats[3];
        sp_defense = stats[4];
        speed = stats[5];
        this->base_speed = base_speed;
        cur_hp = stats[0];
        this->move_one_id = move_one_id;
        this->move_two_id = move_two_id;
        this->poke_id = poke_id;
    }
};

class items_t {
public:
    int num_pokeballs, num_potions, num_revives;
    items_t(int num_pokeballs, int num_potions, int num_revives) {
        this->num_pokeballs = num_pokeballs;
        this->num_potions = num_potions;
        this->num_revives = num_revives;
    }
};

vector<trainer_pokemon> play_arr;
items_t trainer_bag(10, 10, 10);

void io_init_terminal(void) {
    initscr();
    raw();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
    init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
    init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
    init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
    init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
    init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
}

void io_reset_terminal(void) {
    endwin();
    
    while (io_head) {
        io_tail = io_head;
        io_head = io_head->next;
        free(io_tail);
    }
    
    io_tail = NULL;
}

void io_queue_message(const char *format, ...) {
    io_message_t *tmp;
    va_list ap;
    
    if (!(tmp = (io_message_t *)malloc(sizeof (*tmp)))) {
        perror("malloc");
        exit(1);
    }
    
    tmp->next = NULL;
    
    va_start(ap, format);
    
    vsnprintf(tmp->msg, sizeof (tmp->msg), format, ap);
    
    va_end(ap);
    
    if (!io_head) {
        io_head = io_tail = tmp;
    } else {
        io_tail->next = tmp;
        io_tail = tmp;
    }
}

static void io_print_message_queue(uint32_t y, uint32_t x) {
    while (io_head) {
        io_tail = io_head;
        attron(COLOR_PAIR(COLOR_CYAN));
        mvprintw(y, x, "%-80s", io_head->msg);
        attroff(COLOR_PAIR(COLOR_CYAN));
        io_head = io_head->next;
        if (io_head) {
            attron(COLOR_PAIR(COLOR_CYAN));
            mvprintw(y, x + 70, "%10s", " --more-- ");
            attroff(COLOR_PAIR(COLOR_CYAN));
            refresh();
            getch();
        }
        free(io_tail);
    }
    io_tail = NULL;
}

/**************************************************************************
 * Compares trainer distances from the PC according to the rival distance *
 * map.  This gives the approximate distance that the PC must travel to   *
 * get to the trainer (doesn't account for crossing buildings).  This is  *
 * not the distance from the NPC to the PC unless the NPC is a rival.     *
 *                                                                        *
 * Not a bug.                                                             *
 **************************************************************************/
static int compare_trainer_distance(const void *v1, const void *v2) {
    const character_t *const *c1 = (const character_t* const*)v1;
    const character_t *const *c2 = (const character_t* const*)v2;
    
    return (world.rival_dist[(*c1)->pos[dim_y]][(*c1)->pos[dim_x]] -
            world.rival_dist[(*c2)->pos[dim_y]][(*c2)->pos[dim_x]]);
}

static character_t *io_nearest_visible_trainer() {
    character_t **c, *n;
    uint32_t x, y, count;
    
    c = (character_t **)malloc(world.cur_map->num_trainers * sizeof (*c));
    
    /* Get a linear list of trainers */
    for (count = 0, y = 1; y < MAP_Y - 1; y++) {
        for (x = 1; x < MAP_X - 1; x++) {
            if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
                &world.pc) {
                c[count++] = world.cur_map->cmap[y][x];
            }
        }
    }
    
    /* Sort it by distance from PC */
    qsort(c, count, sizeof (*c), compare_trainer_distance);
    
    n = c[0];
    
    free(c);
    
    return n;
}

void io_display() {
    uint32_t y, x;
    character_t *c;
    
    clear();
    for (y = 0; y < MAP_Y; y++) {
        for (x = 0; x < MAP_X; x++) {
            if (world.cur_map->cmap[y][x]) {
                mvaddch(y + 1, x, world.cur_map->cmap[y][x]->symbol);
            } else {
                switch (world.cur_map->map[y][x]) {
                    case ter_boulder:
                    case ter_mountain:
                        attron(COLOR_PAIR(COLOR_MAGENTA));
                        mvaddch(y + 1, x, '%');
                        attroff(COLOR_PAIR(COLOR_MAGENTA));
                        break;
                    case ter_tree:
                    case ter_forest:
                        attron(COLOR_PAIR(COLOR_GREEN));
                        mvaddch(y + 1, x, '^');
                        attroff(COLOR_PAIR(COLOR_GREEN));
                        break;
                    case ter_path:
                    case ter_exit:
                        attron(COLOR_PAIR(COLOR_YELLOW));
                        mvaddch(y + 1, x, '#');
                        attroff(COLOR_PAIR(COLOR_YELLOW));
                        break;
                    case ter_mart:
                        attron(COLOR_PAIR(COLOR_BLUE));
                        mvaddch(y + 1, x, 'M');
                        attroff(COLOR_PAIR(COLOR_BLUE));
                        break;
                    case ter_center:
                        attron(COLOR_PAIR(COLOR_RED));
                        mvaddch(y + 1, x, 'C');
                        attroff(COLOR_PAIR(COLOR_RED));
                        break;
                    case ter_grass:
                        attron(COLOR_PAIR(COLOR_GREEN));
                        mvaddch(y + 1, x, ':');
                        attroff(COLOR_PAIR(COLOR_GREEN));
                        break;
                    case ter_clearing:
                        attron(COLOR_PAIR(COLOR_GREEN));
                        mvaddch(y + 1, x, '.');
                        attroff(COLOR_PAIR(COLOR_GREEN));
                        break;
                    default:
                        /* Use zero as an error symbol, since it stands out somewhat, and it's *
                         * not otherwise used.                                                 */
                        attron(COLOR_PAIR(COLOR_CYAN));
                        mvaddch(y + 1, x, '0');
                        attroff(COLOR_PAIR(COLOR_CYAN));
                }
            }
        }
    }
    
    mvprintw(23, 1, "PC position is (%2d,%2d) on map %d%cx%d%c.",
             world.pc.pos[dim_x],
             world.pc.pos[dim_y],
             abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)),
             world.cur_idx[dim_x] - (WORLD_SIZE / 2) >= 0 ? 'E' : 'W',
             abs(world.cur_idx[dim_y] - (WORLD_SIZE / 2)),
             world.cur_idx[dim_y] - (WORLD_SIZE / 2) <= 0 ? 'N' : 'S');
    mvprintw(22, 1, "%d known %s.", world.cur_map->num_trainers,
             world.cur_map->num_trainers > 1 ? "trainers" : "trainer");
    mvprintw(22, 30, "Nearest visible trainer: ");
    if ((c = io_nearest_visible_trainer())) {
        attron(COLOR_PAIR(COLOR_RED));
        mvprintw(22, 55, "%c at %d %c by %d %c.",
                 c->symbol,
                 abs(c->pos[dim_y] - world.pc.pos[dim_y]),
                 ((c->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ?
                  'N' : 'S'),
                 abs(c->pos[dim_x] - world.pc.pos[dim_x]),
                 ((c->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ?
                  'W' : 'E'));
        attroff(COLOR_PAIR(COLOR_RED));
    } else {
        attron(COLOR_PAIR(COLOR_BLUE));
        mvprintw(22, 55, "NONE.");
        attroff(COLOR_PAIR(COLOR_BLUE));
    }
    
    io_print_message_queue(0, 0);
    
    refresh();
}

static void io_scroll_trainer_list(char (*s)[40], uint32_t count) {
    uint32_t offset;
    uint32_t i;
    
    offset = 0;
    
    while (1) {
        for (i = 0; i < 13; i++) {
            mvprintw(i + 6, 19, " %-40s ", s[i + offset]);
        }
        switch (getch()) {
            case KEY_UP:
                if (offset) {
                    offset--;
                }
                break;
            case KEY_DOWN:
                if (offset < (count - 13)) {
                    offset++;
                }
                break;
            case 27:
                return;
        }
        
    }
}


static void io_list_trainers_display(character_t **c,
                                     uint32_t count) {
    uint32_t i;
    char (*s)[40]; /* pointer to array of 40 char */
    
    s = (char (*)[40])malloc(count * sizeof (*s));
    
    mvprintw(3, 19, " %-40s ", "");
    /* Borrow the first element of our array for this string: */
    snprintf(s[0], 40, "You know of %d trainers:", count);
    mvprintw(4, 19, " %-40s ", s);
    mvprintw(5, 19, " %-40s ", "");
    
    for (i = 0; i < count; i++) {
        snprintf(s[i], 40, "%16s %c: %2d %s by %2d %s",
                 char_type_name[c[i]->npc->ctype],
                 c[i]->symbol,
                 abs(c[i]->pos[dim_y] - world.pc.pos[dim_y]),
                 ((c[i]->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ?
                  "North" : "South"),
                 abs(c[i]->pos[dim_x] - world.pc.pos[dim_x]),
                 ((c[i]->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ?
                  "West" : "East"));
        if (count <= 13) {
            /* Handle the non-scrolling case right here. *
             * Scrolling in another function.            */
            mvprintw(i + 6, 19, " %-40s ", s[i]);
        }
    }
    
    if (count <= 13) {
        mvprintw(count + 6, 19, " %-40s ", "");
        mvprintw(count + 7, 19, " %-40s ", "Hit escape to continue.");
        while (getch() != 27 /* escape */)
            ;
    } else {
        mvprintw(19, 19, " %-40s ", "");
        mvprintw(20, 19, " %-40s ", "Use arrows to scroll, escape to continue.");
        io_scroll_trainer_list(s, count);
    }
    
    free(s);
}

static void io_list_trainers() {
    character_t **c;
    uint32_t x, y, count;
    
    c = (character_t **)malloc(world.cur_map->num_trainers * sizeof (*c));
    
    /* Get a linear list of trainers */
    for (count = 0, y = 1; y < MAP_Y - 1; y++) {
        for (x = 1; x < MAP_X - 1; x++) {
            if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
                &world.pc) {
                c[count++] = world.cur_map->cmap[y][x];
            }
        }
    }
    
    /* Sort it by distance from PC */
    qsort(c, count, sizeof (*c), compare_trainer_distance);
    
    /* Display it */
    io_list_trainers_display(c, count);
    free(c);
    
    /* And redraw the map */
    io_display();
}

void pokemart() {
    trainer_bag.num_potions = 10;
    trainer_bag.num_pokeballs = 10;
    trainer_bag.num_revives = 10;
    mvprintw(0, 0, "Welcome to the Pokemart! Your supplies have been restored!");
    refresh();
    getch();
}

void center() {
    int i;
    for (i = 0; i <= 6; i++) {
        play_arr[i].cur_hp = play_arr[i].hp;
    }
    mvprintw(0, 0, "Welcome to the center! Your Pokemon have been healed!");
    refresh();
    getch();
}

vector<int> moves_finder(int rand_poke, int level, int *moves_start, int *moves_end, vector<pokemon_moves> pm_arr) {
    int i;
    vector<int> possible_moves;
    
    for(i = 0; i < (int)pm_arr.size(); i++) {
        if(pm_arr[i].pokemon_id == pokeArr[rand_poke].species_id) {
            *moves_start = i;
            break;
        }
    }
    for(i = *moves_start; i < (int)pm_arr.size(); i++) {
        if(pm_arr[i].pokemon_id != pokeArr[rand_poke].species_id) {
            *moves_end = i;
            break;
        }
        else if(i == (int)pm_arr.size() - 1) {
            *moves_end = (int)pm_arr.size();
            break;
        }
    }
    
    for(i = *moves_start; i < *moves_end; i++) {
        if(pm_arr[i].pokemon_move_method_id == 1 && pm_arr[i].level <= level && pm_arr[i].level != 0) {
            possible_moves.push_back(pm_arr[i].move_id);
        }
    }
    
    return possible_moves;
}

trainer_pokemon createPokemon() {
    int rand_poke, rand_move, i, man_distance, level, moves_start, moves_end, move_one_id, move_two_id, stat_count;
    string move_one, move_two, gender, shiny;
    vector<int> possible_moves;
    vector<int> base_stats;
    vector<int> ivs;
    vector<int> stats;
    
    rand_poke = (rand() % 1092) + 1;
    
    man_distance = abs(world.cur_idx[dim_x] - 200) + abs(world.cur_idx[dim_y] - 200);
    
    if(man_distance == 1 || man_distance == 0) {
        level = 1;
    }
    else if(man_distance > 200) {
        level = (rand() % (100 - ((man_distance - 200) / 2) + 1)) + ((man_distance - 200) / 2);
    }
    else {
        level = (rand() % (man_distance / 2)) + 1;
    }

    moves_start = moves_end = -1;
    if(pokeArr[rand_poke].species_id < 808) {
        possible_moves = moves_finder(rand_poke, level, &moves_start, &moves_end, pm_arr);
    }
    else {
        possible_moves = moves_finder(rand_poke, level, &moves_start, &moves_end, pm_arr_end);
    }
    
    move_one_id = move_two_id = -1;
    rand_move = rand() % (int)possible_moves.size();
    move_one_id = possible_moves[rand_move];
    
    if((int)possible_moves.size() > 1) {
        while(true) {
            rand_move = rand() % (int)possible_moves.size();
            if(possible_moves[rand_move] != move_one_id) {
                move_two_id = possible_moves[rand_move];
                break;
            }
        }
    }
    
    for(i = 0; i < (int)movesArr.size(); i++) {
        if(move_one_id == movesArr[i].id) {
            move_one = movesArr[i].name;
            move_one[0] = toupper(move_one[0]);
            break;
        }
    }
    if(move_two_id != -1) {
        for(i = 0; i < (int)movesArr.size(); i++) {
            if(move_two_id == movesArr[i].id) {
                move_two = movesArr[i].name;
                move_two[0] = toupper(move_two[0]);
                break;
            }
        }
    }
    
    for(i = 0; i < (int)poke_stats_vec.size(); i++) {
        if(poke_stats_vec[i].pokemon_id == pokeArr[rand_poke].id) {
            break;
        }
    }
    for(stat_count = 0; stat_count < 6; stat_count++) {
        base_stats.push_back(poke_stats_vec[i + stat_count].base_stat);
    }
    for(i = 0; i < 6; i++) {
        ivs.push_back(rand() % 16);
    }
    stats.push_back(((((base_stats[0] + ivs[0]) * 2) * level) / 100) + level + 10);
    for(i = 1; i < 6; i++) {
        stats.push_back(((((base_stats[i] + ivs[i]) * 2) * level) / 100) + 5);
    }
    
    if(rand() % 2 == 0 ) {
        gender = "Male";
    } else {
        gender = "Female";
    }
    
    if(rand() % 8192 == 0) {
        shiny = "Positive";
    } else {
        shiny = "Negative";
    }
    
    pokeArr[rand_poke].name[0] = toupper(pokeArr[rand_poke].name[0]);
    
    trainer_pokemon generated_poke(pokeArr[rand_poke].name, gender, shiny, move_one, move_two, level, stats, move_one_id, move_two_id, base_stats[5], pokeArr[rand_poke].id);
    
    return generated_poke;
}

void battleScreen(trainer_pokemon opponent, int cur_pokemon, int type) {
	clear();
    
    if(type == 0) {
        mvprintw(1, 1, "Pokemon!");
    }
    else {
        mvprintw(1, 1, "You are in a battle!");
    }
    
    mvprintw(3, 1, "Opponents Pokemon: %s, Level: %d", opponent.name.c_str(), opponent.level);
    mvprintw(4, 1, "Shiny: %s", opponent.shiny.c_str());
    mvprintw(5, 1, "Move One: %s", opponent.move_one.c_str());
    mvprintw(6, 1, "Move Two: %s", opponent.move_two.c_str());
    mvprintw(7, 1, "HP: %d/%d", opponent.cur_hp, opponent.hp);

    if (flee == true) {
    mvprintw(10, 1, "Pokemon Name: %s, Pokemon Level: %d", play_arr[cur_pokemon].name.c_str(), play_arr[cur_pokemon].level);
	mvprintw(11, 1, "HP: %d/%d", play_arr[cur_pokemon].cur_hp, play_arr[cur_pokemon].hp);
    mvprintw(13, 1, "1. Fight");
    mvprintw(14, 1, "2. Bag");
    mvprintw(15, 1, "3. Run");
    mvprintw(16, 1, "4. Pokemon");
    mvprintw(17, 1, "5. Flee");
	mvprintw(18, 1, "Choose 1-5");
    } else {
            mvprintw(10, 1, "Pokemon Name: %s, Pokemon Level: %d", play_arr[cur_pokemon].name.c_str(), play_arr[cur_pokemon].level);
	mvprintw(11, 1, "HP: %d/%d", play_arr[cur_pokemon].cur_hp, play_arr[cur_pokemon].hp);
    mvprintw(13, 1, "1. Fight");
    mvprintw(14, 1, "2. Bag");
    mvprintw(15, 1, "3. Run");
    mvprintw(16, 1, "4. Pokemon");
	mvprintw(18, 1, "Choose 1-4");
    }
}

void showBag(int open_type) {
	clear();
    
    mvprintw(1, 1, "Inventory");
    mvprintw(3, 1, "1. Pokeballs: %d", trainer_bag.num_pokeballs);
    mvprintw(4, 1, "2. Potions: %d", trainer_bag.num_potions);
    mvprintw(5, 1, "3. Revives: %d", trainer_bag.num_revives);
    mvprintw(7, 1, "Press any to close.");
}


void cheater() {
    clear();
    char cheat;

    mvprintw(1, 1, "You should be ashamed of yourself...");
    mvprintw(2, 1, "1: moar pokeballs");
    mvprintw(3, 1, "2: moar potions");
    mvprintw(4, 1, "3: moar revives");
    mvprintw(5, 1, "4: moar pokemon");
    mvprintw(6, 1, "5: moar healthy pokemon");
    mvprintw(7, 1, "6: moar stronk pokemon");
    mvprintw(8, 1, "7: moar cowardly options");
    
    cheat = getch();

    if (cheat == '1') {
        clear();
        mvprintw(1, 1, "Downloading cheaterballs... you wouldn't download a car would you?");
        mvprintw(2, 1, "Press 4 to return to game!");
        trainer_bag.num_pokeballs = 100000;
    }
    if (cheat == '2') {
        clear();
        mvprintw(1, 1, "Heres some potions cheater... I'd smell those first, be a shame if they were past the expiration date haha.. jk.. unless?");
        mvprintw(2, 1, "Press 4 to return to game!");
        trainer_bag.num_potions = 100000;
    }
    if (cheat == '3') {
        clear();
        mvprintw(1, 1, "bah bah bah bah staying aliiiiiiiiiiveeeee");
        mvprintw(2, 1, "Press 4 to return to game!");
        trainer_bag.num_revives = 100000;
    }
    if (cheat == '4') {
        clear();
        mvprintw(1, 1, "Poor things...");
        mvprintw(2, 1, "Press 4 to return to game!");
        for (int i = 0; i < 6; i++) {
        trainer_pokemon encounter = createPokemon();
        play_arr.push_back(encounter);
        }
    }
    if (cheat == '5') {
        clear();
        mvprintw(1, 1, "Only cause I like them");
        mvprintw(2, 1, "Press 4 to return to game!");
        for (int i = 0; i < 6; i++) {
        play_arr[i].cur_hp = play_arr[i].hp;
        }
    }
    if (cheat == '6') {
        clear();
        mvprintw(1, 1, "Wish they would attack you tbh");
        mvprintw(2, 1, "Press 4 to return to game!");
        for (int i = 0; i < 6; i++) {
        play_arr[i].hp += 100;
        play_arr[i].cur_hp = play_arr[i].hp;
        play_arr[i].level = 100;
        }
    }
    if (cheat == '7') {
        clear();
        mvprintw(1, 1, "Pretty cowardly my friend");
        mvprintw(2, 1, "Press 4 to return to game!");
        flee = true;
    }
}
int heal(char choice) {
	int i;
    char poke_choice;
    
	clear();
    
    mvprintw(1, 1, "Which pokemon do you want to heal?");
	mvprintw(12, 1, "Press any other key to escape.");
    for(i = 0; i < 6; i++) {
        if(i < (int)play_arr.size()) {
            mvprintw(3 + i, 1, "%d. %s: Level %d, HP: %d/%d", i + 1, play_arr[i].name.c_str(), play_arr[i].level, play_arr[i].cur_hp, play_arr[i].hp);
        }
        else {
            mvprintw(3 + i, 1, "%d. ", i + 1);
        }
    }
    
    while(true) {
        poke_choice = getch();
        if(poke_choice == '1' || poke_choice == '2' || poke_choice == '3' || poke_choice == '4' || poke_choice == '5' || poke_choice == '6') {
            poke_choice -= 48;
            if((int)poke_choice > (int)play_arr.size()) {
                mvprintw(10, 1, "Not enough pokemon!");
                getch();
                mvprintw(10, 1, "                                 ");
            }
            else {
                poke_choice--;
                break;
            }
        }
        else {
			return 4;
        }
    }
    
    if(choice == '2') {
        if(play_arr[poke_choice].cur_hp > 0 && play_arr[poke_choice].cur_hp < play_arr[poke_choice].hp) {
            trainer_bag.num_potions--;
            play_arr[poke_choice].cur_hp += 20;
            if(play_arr[poke_choice].cur_hp > play_arr[poke_choice].hp) {
                play_arr[poke_choice].cur_hp = play_arr[poke_choice].hp;
            }
            mvprintw(10, 1, "Potions Left %d:", trainer_bag.num_potions);
            getch();
            return 1;
        }
        else {
            mvprintw(10, 1, "Cannot heal!");
            getch();
            return 4;
        }
    }
    else {
        if(play_arr[poke_choice].cur_hp <= 0) {
            trainer_bag.num_revives--;
            play_arr[poke_choice].cur_hp = play_arr[poke_choice].hp / 2;
            mvprintw(10, 1, "Revivals left %d:", trainer_bag.num_revives);
            getch();
            return 1;
        }
        else {
            mvprintw(10, 1, "Cannot revive!");
            getch();
            return 4;
        }
    }
}

int io_bag(char choice, int cur_pokemon) {
    if(choice == '1') {
		if(trainer_bag.num_pokeballs > 0) {
			if((int)play_arr.size() < 6) {
				trainer_bag.num_pokeballs--;
				mvprintw(10, 1, "Pokeballs left %d:", trainer_bag.num_pokeballs);
				getch();
				return 2;
			}
			else {
				mvprintw(10, 1, "No more room for pokemon!");
				getch();
				return 3;
			}			
		}
		else {
            mvprintw(10, 1, "No more pokeballs remaining!");
            getch();
            return 4;			
		}
    }
    else if(choice == '2') {
		if(trainer_bag.num_potions > 0) {
			return heal(choice);
		}
        else {
            mvprintw(10, 1, "Potions empty!");
            getch();
            return 4;			
		}
    }
    else if(choice == '3') {
		if(trainer_bag.num_revives > 0) {
			return heal(choice);
		}
        else {
            mvprintw(10, 1, "Revives empty!");
            getch();
            return 4;			
		}
    }
    else {
        return 4;
    }
}

int io_pokemon_list(int cur_pokemon, int type) {
    int i, turn_used_type;
    char choice;
    trainer_pokemon temp_poke = play_arr[cur_pokemon];
    
	clear();
    
    mvprintw(1, 1, "Pokemon: ");
    for(i = 0; i < 6; i++) {
        if(i < (int)play_arr.size()) {
            mvprintw(3 + i, 1, "%d. %s: Level %d, HP: %d/%d", i + 1, play_arr[i].name.c_str(), play_arr[i].level, play_arr[i].cur_hp, play_arr[i].hp);
        }
        else {
            mvprintw(3 + i, 1, "%d. ", i + 1);
        }
    }
    
    mvprintw(10, 1, "Choose a pokemon!");
    if(type == 0) {
        mvprintw(14, 1, "Press any key to close.");
    }
    while(true) {
        choice = getch();
        if(choice == '1' || choice == '2' || choice == '3' || choice == '4' || choice == '5' || choice == '6') {
            choice -= 48;
            if((int)choice > (int)play_arr.size()) {
                mvprintw(12, 1, "Not enough pokemon!");
                getch();
                mvprintw(12, 1, "                                 ");
            }
            else {
                choice--;
                if(play_arr[choice].cur_hp <= 0) {
                    mvprintw(12, 1, "HP Empty!");
                    getch();
                    mvprintw(12, 1, "                                  ");
                }
                else if(choice == cur_pokemon) {
                    mvprintw(12, 1, "Pokemon already current!");
                    getch();
                    mvprintw(12, 1, "                                  ");
                }
                else {
                    play_arr[cur_pokemon] = play_arr[choice];
                    play_arr[choice] = temp_poke;
                    mvprintw(12, 1, "Using %s.", play_arr[cur_pokemon].name.c_str());
                    turn_used_type = 1;
                    getch();
                    break;
                }
            }
        }
        else {
            if(type == 1) {
                mvprintw(12, 1, "Choose a pokemon!");
                getch();
                mvprintw(12, 1, "                          ");
            }
            else {
                turn_used_type =  4;
                break;
            }
        }
    }
    
    return turn_used_type;
}

string attack(int move, trainer_pokemon *attacker, trainer_pokemon *defender, int type) {
    int i;
	double damage, random;
	double crit = 1.0;
    double stab = 1.0;
	
	if(rand() % 100 < movesArr[move].accuracy) {
		if(movesArr[move].power != -1) {
			if(rand() % 256 < (attacker->base_speed / 2)) {
				crit = 1.5;
			}
            
            for(i = 0; i < (int)pt_arr.size(); i++) {
                if(pt_arr[i].pokemon_id == attacker->poke_id) {
                    break;
                }
            }
            if(movesArr[move].type_id != -1) {
                if(pt_arr[i].type_id == movesArr[move].type_id) {
                    stab = 1.5;
                }
                i++;
                if(i < (int)pt_arr.size()) {
                    if(pt_arr[i].type_id == movesArr[move].type_id) {
                        stab = 1.5;
                    }
                }
            }
            
			random = (rand() % 16) + 85.0;
			random /= 100.0;
			
			damage = ((((((2.0 * attacker->level) / 5.0) + 2.0) * movesArr[move].power * (attacker->attack / defender->defense)) / 50.0) + 2.0) * crit * random * stab * 1.0;
			defender->cur_hp -= (int)damage;
			if(defender->cur_hp <= 0) {
				defender->cur_hp = 0;
			}
            
			if(crit == 1.5) {
				return attacker->name + " used " + movesArr[move].name + ", it's a critical hit!";
			}
			else {
				return attacker->name + " used " + movesArr[move].name + ".";
			}
		}
		else {
			return attacker->name + " used " + movesArr[move].name + ", it did no damage!";
		}
	}
	else {
		return attacker->name + "'s move has missed!";
	}
}

void clear_line() {
	int i;
	for(i = 0; i < 80; i++) {
		mvprintw(18, i, " ");
	}
}

void io_battle(character_t *aggressor, character_t *defender) {
    character_t *npc;
    vector<trainer_pokemon> npcArr;
    int i;
    int num_pokemon = 1;
    int pc_poke_check = 0;
    
    for(i = 0; i < (int)play_arr.size(); i++) {
        if(play_arr[i].cur_hp == 0) {
            pc_poke_check++;
        }
    }
    if(pc_poke_check != (int)play_arr.size()) {
        for(i = 0; i < 5; i++) {
            if(rand() % 5 == 0 || rand() % 5 == 1 || rand() % 5 == 2) {
                num_pokemon++;
            }
            else {
                break;
            }
        }
        
        for(i = 0; i < num_pokemon; i++) {
            npcArr.push_back(createPokemon());
        }
        
        int turn_used, cur_pokemon, chosen_move, encounters_move, num_fainted;
        char choice, item_choice, move_choice;
        int npc_cur_pokemon = 0;
        bool valid_choice, has_more_pokemon;
        
        has_more_pokemon = false;
        chosen_move = -1;
        
        num_fainted = 0;
        cur_pokemon = 0;
        
        for(i = 0; i < (int)play_arr.size(); i++) {
            if(play_arr[i].cur_hp != 0) {
                cur_pokemon = i;
                break;
            }
        }
        
        battleScreen(npcArr[npc_cur_pokemon], cur_pokemon, 1);
        
        choice = getch();
        while(true) {
            if(choice == '1') {
				valid_choice = true;
				clear();
				mvprintw(1, 1, "Choose a move!");
				mvprintw(3, 1, "1. %s", play_arr[cur_pokemon].move_one.c_str());
				if(play_arr[cur_pokemon].move_two_id != -1) {
					mvprintw(4, 1, "2. %s", play_arr[cur_pokemon].move_two.c_str());
				}
				mvprintw(6, 1, "Press any key to close");
				move_choice = getch();
				if(move_choice == '1') {
					chosen_move = play_arr[cur_pokemon].move_one_id;
					turn_used = 0;
				}
				else if(move_choice == '2') {
					chosen_move = play_arr[cur_pokemon].move_two_id;
					turn_used = 0;
				}
				else {
					turn_used = 4;
				}
            }
            else if(choice == '2') {
                valid_choice = true;
                showBag(1);
                item_choice = getch();
                if(item_choice == '1') {
                    mvprintw(9, 1, "Invalid option!");
                    getch();
                    turn_used = 4;
                }
                else {
                    turn_used = io_bag(item_choice, cur_pokemon);
                    battleScreen(npcArr[npc_cur_pokemon], cur_pokemon, 1);
                }
            }
            else if(choice == '3') {
                valid_choice = true;
                mvprintw(18, 1, "Invalid option!");
                turn_used = 4;
                getch();
            }
            else if(choice == '4') {
                valid_choice = true;
                turn_used = io_pokemon_list(cur_pokemon, 0);
            }
            else if(choice == '5') {
                mvprintw(18, 1, "Succesfully escaped! Press any key to continue.");
                break;
            }
            else {
                valid_choice = false;
				clear_line();
                mvprintw(18, 1, "Invalid option!");
                getch();
            }
            
            if(valid_choice) {
                battleScreen(npcArr[npc_cur_pokemon], cur_pokemon, 1);
                
                if(npcArr[npc_cur_pokemon].move_two_id != -1) {
                    if(rand() % 2 == 0) {
                        encounters_move = npcArr[npc_cur_pokemon].move_one_id;
                    }
                    else {
                        encounters_move = npcArr[npc_cur_pokemon].move_two_id;
                    }
                }
                else {
                    encounters_move = npcArr[npc_cur_pokemon].move_one_id;
                }
                
                if(turn_used == 0) {
					clear_line();
                    if(movesArr[chosen_move].priority > movesArr[encounters_move].priority) {
						mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &npcArr[npc_cur_pokemon], 1).c_str());
						getch();
						clear_line();
                        if(npcArr[npc_cur_pokemon].cur_hp == 0) {
                            mvprintw(18, 1, "Enemy trainers %s fainted!", npcArr[npc_cur_pokemon].name.c_str());
                            getch();
							clear_line();
                            for(i = 0; i < (int)npcArr.size(); i++) {
                                if(npcArr[i].cur_hp > 0) {
                                    has_more_pokemon = true;
                                    break;
                                }
                                else {
                                    has_more_pokemon = false;
                                }
                            }
                            if(has_more_pokemon) {
                                npc_cur_pokemon = i;
                                mvprintw(18, 1, "Enemy has chosen %s!", npcArr[npc_cur_pokemon].name.c_str());
                                getch();
                            }
                            else {
                                mvprintw(18, 1, "You have won!", npcArr[npc_cur_pokemon].name.c_str());
								getch();
                                break;
                            }
                        }
                        else {
							mvprintw(18, 1, "%s", attack(encounters_move, &npcArr[npc_cur_pokemon], &play_arr[cur_pokemon], 1).c_str());
                            getch();
							clear_line();
                            if(play_arr[cur_pokemon].cur_hp == 0) {
                                for(i = 0; i < (int)play_arr.size(); i++) {
                                    if(play_arr[i].cur_hp == 0) {
                                        num_fainted++;
                                    }
                                }
                                if(num_fainted == (int)play_arr.size()) {
                                    mvprintw(18, 11, "Defeat!");
									getch();
                                    break;
                                }
                                else {
                                    mvprintw(18, 11, "%s HP is empty, choose a different pokemon!", play_arr[cur_pokemon].name.c_str());
                                    getch();
                                    io_pokemon_list(cur_pokemon, 1);
                                    num_fainted = 0;
                                }
                            }
                        }
                    }
                    else if(movesArr[chosen_move].priority < movesArr[encounters_move].priority) {
                        mvprintw(18, 1, "%s", attack(encounters_move, &npcArr[npc_cur_pokemon], &play_arr[cur_pokemon], 1).c_str());
                        getch();
						clear_line();
                        if(play_arr[cur_pokemon].cur_hp == 0) {
                            for(i = 0; i < (int)play_arr.size(); i++) {
                                if(play_arr[i].cur_hp == 0) {
                                    num_fainted++;
                                }
                            }
                            if(num_fainted == (int)play_arr.size()) {
                                mvprintw(18, 1, "Defeat!");
								getch();
                                break;
                            }
                            else {
                                mvprintw(18, 1, "%s HP is empty, choose a different pokemon!", play_arr[cur_pokemon].name.c_str());
                                getch();
                                io_pokemon_list(cur_pokemon, 1);
                                num_fainted = 0;
                            }
                        }
                        else {
                            mvprintw(18, 11, "%s", attack(chosen_move, &play_arr[cur_pokemon], &npcArr[npc_cur_pokemon], 1).c_str());
                            getch();
							clear_line();
                            if(npcArr[npc_cur_pokemon].cur_hp == 0) {
                                mvprintw(18, 1, "Enemies %s has fainted!", npcArr[npc_cur_pokemon].name.c_str());
                                getch();
								clear_line();
                                for(i = 0; i < (int)npcArr.size(); i++) {
                                    if(npcArr[i].cur_hp > 0) {
                                        has_more_pokemon = true;
                                        break;
                                    }
                                    else {
                                        has_more_pokemon = false;
                                    }
                                }
                                if(has_more_pokemon) {
                                    npc_cur_pokemon = i;
                                    mvprintw(18, 1, "Enemy trainer chooses %s!", npcArr[npc_cur_pokemon].name.c_str());
                                    getch();
                                }
                                else {
                                    mvprintw(18, 1, "Victory!", npcArr[npc_cur_pokemon].name.c_str());
									getch();
                                    break;
                                }
                            }
                        }
                    }
                    else {
                        if(play_arr[cur_pokemon].speed >= npcArr[npc_cur_pokemon].speed) {
                            mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &npcArr[npc_cur_pokemon], 1).c_str());
							getch();
							clear_line();
                            if(npcArr[npc_cur_pokemon].cur_hp == 0) {
                                mvprintw(18, 1, "The opponents %s fainted!", npcArr[npc_cur_pokemon].name.c_str());
                                getch();
								clear_line();
                                for(i = 0; i < (int)npcArr.size(); i++) {
                                    if(npcArr[i].cur_hp > 0) {
                                        has_more_pokemon = true;
                                        break;
                                    }
                                    else {
                                        has_more_pokemon = false;
                                    }
                                }
                                if(has_more_pokemon) {
                                    npc_cur_pokemon = i;
                                    mvprintw(18, 1, "Your opponent sent out %s!", npcArr[npc_cur_pokemon].name.c_str());
                                    getch();
                                }
                                else {
                                    mvprintw(18, 1, "You defeated the trainer!", npcArr[npc_cur_pokemon].name.c_str());
									getch();
                                    break;
                                }
                            }
                            else {
                                mvprintw(18, 1, "%s", attack(encounters_move, &npcArr[npc_cur_pokemon], &play_arr[cur_pokemon], 1).c_str());
                                getch();
								clear_line();
                                if(play_arr[cur_pokemon].cur_hp == 0) {
                                    for(i = 0; i < (int)play_arr.size(); i++) {
                                        if(play_arr[i].cur_hp == 0) {
                                            num_fainted++;
                                        }
                                    }
                                    if(num_fainted == (int)play_arr.size()) {
                                        mvprintw(18, 1, "All your pokemon have fainted, you lose!");
										getch();
                                        break;
                                    }
                                    else {
                                        mvprintw(18, 1, "%s fainted! Select a new pokemon!", play_arr[cur_pokemon].name.c_str());
                                        getch();
                                        io_pokemon_list(cur_pokemon, 1);
                                        num_fainted = 0;
                                    }
                                }
                            }
                        }
                        else {
                            mvprintw(18, 1, "%s", attack(encounters_move, &npcArr[npc_cur_pokemon], &play_arr[cur_pokemon], 1).c_str());
                            getch();
							clear_line();
                            if(play_arr[cur_pokemon].cur_hp == 0) {
                                for(i = 0; i < (int)play_arr.size(); i++) {
                                    if(play_arr[i].cur_hp == 0) {
                                        num_fainted++;
                                    }
                                }
                                if(num_fainted == (int)play_arr.size()) {
                                    mvprintw(18, 1, "All your pokemon have fainted, you lose!");
									getch();
                                    break;
                                }
                                else {
                                    mvprintw(18, 1, "%s fainted! Select a new pokemon!", play_arr[cur_pokemon].name.c_str());
                                    getch();
                                    io_pokemon_list(cur_pokemon, 1);
                                    num_fainted = 0;
                                }
                            }
                            else {
                                mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &npcArr[npc_cur_pokemon], 1).c_str());
                                getch();
								clear_line();
                                if(npcArr[npc_cur_pokemon].cur_hp == 0) {
                                    mvprintw(18, 1, "The opponents %s fainted!", npcArr[npc_cur_pokemon].name.c_str());
                                    getch();
									clear_line();
                                    for(i = 0; i < (int)npcArr.size(); i++) {
                                        if(npcArr[i].cur_hp > 0) {
                                            has_more_pokemon = true;
                                            break;
                                        }
                                        else {
                                            has_more_pokemon = false;
                                        }
                                    }
                                    if(has_more_pokemon) {
                                        npc_cur_pokemon = i;
                                        mvprintw(18, 1, "Your opponent sent out %s!", npcArr[npc_cur_pokemon].name.c_str());
                                        getch();
                                    }
                                    else {
                                        mvprintw(18, 1, "You defeated the trainer!", npcArr[npc_cur_pokemon].name.c_str());
										getch();
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
                else if(turn_used == 1) {
					clear_line();
                    mvprintw(18, 1, "%s", attack(encounters_move, &npcArr[npc_cur_pokemon], &play_arr[cur_pokemon], 1).c_str());
                    getch();
					clear_line();
                    if(play_arr[cur_pokemon].cur_hp == 0) {
                        for(i = 0; i < (int)play_arr.size(); i++) {
                            if(play_arr[i].cur_hp == 0) {
                                num_fainted++;
                            }
                        }
                        if(num_fainted == (int)play_arr.size()) {
                            mvprintw(18, 1, "All your pokemon have fainted, you lose!");
							getch();
                            break;
                        }
                        else {
                            mvprintw(18, 1, "%s fainted! Select a new pokemon!", play_arr[cur_pokemon].name.c_str());
                            getch();
                            io_pokemon_list(cur_pokemon, 1);
                            num_fainted = 0;
                        }
                    }
                }
            }
            battleScreen(npcArr[npc_cur_pokemon], cur_pokemon, 1);
            choice = getch();
        }

        if (aggressor->pc) {
            npc = defender;
        } else {
            npc = aggressor;
        }
        
        npc->npc->defeated = 1;
        if (npc->npc->ctype == char_hiker || npc->npc->ctype == char_rival) {
            npc->npc->mtype = move_wander;
        }
    }
    else {
        mvprintw(0, 0, "Need to heal pokemon!!");
        getch();
		mvprintw(0, 0, "                                           ");
    }
}
// classes, templates, overloaded operators, stl, inheritance.

uint32_t io_teleport_pc(pair_t dest)
{
  /* Just for fun. And debugging.  Mostly debugging. */

  do {
    dest[dim_x] = rand_range(1, MAP_X - 2);
    dest[dim_y] = rand_range(1, MAP_Y - 2);
  } while (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]                  ||
           move_cost[char_pc][world.cur_map->map[dest[dim_y]]
                                                [dest[dim_x]]] == INT_MAX ||
           world.rival_dist[dest[dim_y]][dest[dim_x]] < 0);

  return 0;
}

void io_teleport_world(pair_t dest)
{
  int x, y;
  
  world.cur_map->cmap[world.pc.pos[dim_y]][world.pc.pos[dim_x]] = NULL;

  mvprintw(0, 0, "Enter x [-200, 200]: ");
  refresh();
  echo();
  curs_set(1);
  mvscanw(0, 21, (char *)"%d", &x);
  mvprintw(0, 0, "Enter y [-200, 200]:          ");
  refresh();
  mvscanw(0, 21, (char *)"%d", &y);
  refresh();
  noecho();
  curs_set(0);

  if (x < -200) {
    x = -200;
  }
  if (x > 200) {
    x = 200;
  }
  if (y < -200) {
    y = -200;
  }
  if (y > 200) {
    y = 200;
  }
  
  x += 200;
  y += 200;

  world.cur_idx[dim_x] = x;
  world.cur_idx[dim_y] = y;

  new_map(1);
  io_teleport_pc(dest);
}

void wildPoke() {
    int odds_escape, turn_used, cur_pokemon, i, chosen_move, encounters_move, num_fainted;
    int attempts = 0;
    char choice, item_choice, move_choice;
    trainer_pokemon encounter = createPokemon();
    bool valid_choice;
    
    chosen_move = -1;
    
    num_fainted = 0;
    cur_pokemon = 0;
    for(i = 0; i < (int)play_arr.size(); i++) {
        if(play_arr[i].cur_hp != 0) {
            cur_pokemon = i;
            break;
        }
    }
    battleScreen(encounter, cur_pokemon, 0);
    
    choice = getch();
    while(true) {
        if(choice == '1') {
            valid_choice = true;
			clear();
            mvprintw(1, 1, "Which move would you like to use?");
            mvprintw(3, 1, "1. %s", play_arr[cur_pokemon].move_one.c_str());
            if(play_arr[cur_pokemon].move_two_id != -1) {
                mvprintw(4, 1, "2. %s", play_arr[cur_pokemon].move_two.c_str());
            }
			mvprintw(6, 1, "Press any other key to exit.");
            move_choice = getch();
            if(move_choice == '1') {
                chosen_move = play_arr[cur_pokemon].move_one_id;
                turn_used = 0;
            }
            else if(move_choice == '2') {
                chosen_move = play_arr[cur_pokemon].move_two_id;
                turn_used = 0;
            }
			else {
				turn_used = 4;
			}
        }
        else if(choice == '2') {
            valid_choice = true;
            showBag(1);
            item_choice = getch();
            turn_used = io_bag(item_choice, cur_pokemon);
            battleScreen(encounter, cur_pokemon, 0);
        }
        else if(choice == '3') {
            valid_choice = true;
            attempts++;
            odds_escape = ((play_arr[cur_pokemon].speed * 32) / ((encounter.speed / 4) % 256)) + 30 * attempts;
            if(rand() % 256 < odds_escape) {
                mvprintw(18, 1, "Succesfully escaped! Press any key to continue.");
                break;
            }
            else {
				mvprintw(18, 1, "                                                                                                       ");
                mvprintw(18, 1, "Couldn't escape!");
                getch();
            }
            turn_used = 4;
        }
        else if(choice == '4') {
            valid_choice = true;
            turn_used = io_pokemon_list(cur_pokemon, 0);
        }
        else if(choice == '5') {
            break;
        }
        else {
            valid_choice = false;
			clear_line();
            mvprintw(18, 1, "Invalid Choice.");
            getch();
        }
        
        if(valid_choice) {
            battleScreen(encounter, cur_pokemon, 0);
            
            if(encounter.move_two_id != -1) {
                if(rand() % 2 == 0) {
                    encounters_move = encounter.move_one_id;
                }
                else {
                    encounters_move = encounter.move_two_id;
                }
            }
            else {
                encounters_move = encounter.move_one_id;
            }
            
            if(turn_used == 0) {
				clear_line();
                if(movesArr[chosen_move].priority > movesArr[encounters_move].priority) {
                    mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &encounter, 0).c_str());
					getch();
					clear_line();
                    if(encounter.cur_hp == 0) {
                        mvprintw(18, 1, "%s has fainted!", encounter.name.c_str());
						getch();
                        break;
                    }
                    else {
                        mvprintw(18, 1, "%s", attack(encounters_move, &encounter, &play_arr[cur_pokemon], 0).c_str());
						getch();
                        if(play_arr[cur_pokemon].cur_hp == 0) {
							clear_line();
                            for(i = 0; i < (int)play_arr.size(); i++) {
                                if(play_arr[i].cur_hp == 0) {
                                    num_fainted++;
                                }
                            }
                            if(num_fainted == (int)play_arr.size()) {
                                mvprintw(18, 1, "Defeat");
								getch();
                                break;
                            }
                            else {
                                mvprintw(18, 1, "%s fainted, choose a new pokemon!", play_arr[cur_pokemon].name.c_str());
								getch();
                                io_pokemon_list(cur_pokemon, 1);
                                num_fainted = 0;
                            }
                        }
                    }
                }
                else if(movesArr[chosen_move].priority < movesArr[encounters_move].priority) {
                    mvprintw(18, 1, "%s", attack(encounters_move, &encounter, &play_arr[cur_pokemon], 0).c_str());
                    getch();
					clear_line();
                    if(play_arr[cur_pokemon].cur_hp == 0) {
                        for(i = 0; i < (int)play_arr.size(); i++) {
                            if(play_arr[i].cur_hp == 0) {
                                num_fainted++;
                            }
                        }
                        if(num_fainted == (int)play_arr.size()) {
                            mvprintw(18, 1, "Defeat!");
							getch();
                            break;
                        }
                        else {
                            mvprintw(18, 1, "%s fainted, choose a new pokemon!", play_arr[cur_pokemon].name.c_str());
                            getch();
                            io_pokemon_list(cur_pokemon, 1);
                            num_fainted = 0;
                        }
                    }
                    else {
                        mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &encounter, 0).c_str());
                        getch();
						clear_line();
                        if(encounter.cur_hp == 0) {
                            mvprintw(18, 1, "%s has fainted!", encounter.name.c_str());
							getch();
                            break;
                        }
                    }
                }
                else {
                    if(play_arr[cur_pokemon].speed >= encounter.speed) {
                        mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &encounter, 0).c_str());
						getch();
						clear_line();
                        if(encounter.cur_hp == 0) {
                            mvprintw(18, 1, "%s has fainted!", encounter.name.c_str());
							getch();
                            break;
                        }
                        else {
                            mvprintw(18, 1, "%s", attack(encounters_move, &encounter, &play_arr[cur_pokemon], 1).c_str());
                            getch();
							clear_line();
                            if(play_arr[cur_pokemon].cur_hp == 0) {
                                for(i = 0; i < (int)play_arr.size(); i++) {
                                    if(play_arr[i].cur_hp == 0) {
                                        num_fainted++;
                                    }
                                }
                                if(num_fainted == (int)play_arr.size()) {
                                    mvprintw(18, 1, "Defeat!");
									getch();
                                    break;
                                }
                                else {
                                    mvprintw(18, 1, "%s fainted, choose a new pokemon!", play_arr[cur_pokemon].name.c_str());
                                    getch();
                                    io_pokemon_list(cur_pokemon, 1);
                                    num_fainted = 0;
                                }
                            }
                        }
                    }
                    else {
                        mvprintw(18, 1, "%s", attack(encounters_move, &encounter, &play_arr[cur_pokemon], 0).c_str());
                        getch();
						clear_line();
                        if(play_arr[cur_pokemon].cur_hp == 0) {
                            for(i = 0; i < (int)play_arr.size(); i++) {
                                if(play_arr[i].cur_hp == 0) {
                                    num_fainted++;
                                }
                            }
                            if(num_fainted == (int)play_arr.size()) {
                                mvprintw(18, 1, "Defeat!");
								getch();
                                break;
                            }
                            else {
                                mvprintw(18, 1, "%s fainted, choose a new pokemon!", play_arr[cur_pokemon].name.c_str());
                                getch();
                                io_pokemon_list(cur_pokemon, 1);
                                num_fainted = 0;
                            }
                        }
                        else {
                            mvprintw(18, 1, "%s", attack(chosen_move, &play_arr[cur_pokemon], &encounter, 0).c_str());
                            getch();
                            if(encounter.cur_hp == 0) {
								clear_line();
                                mvprintw(18, 1, "%s fainted!", encounter.name.c_str());
								getch();
                                break;
                            }
                        }
                    }
                }
            }
            else if(turn_used == 1) {
				clear_line();
                mvprintw(18, 1, "%s", attack(encounters_move, &encounter, &play_arr[cur_pokemon], 0).c_str());
                getch();
				clear_line();
                if(play_arr[cur_pokemon].cur_hp == 0) {
                    for(i = 0; i < (int)play_arr.size(); i++) {
                        if(play_arr[i].cur_hp == 0) {
                            num_fainted++;
                        }
                    }
                    if(num_fainted == (int)play_arr.size()) {
                        mvprintw(18, 1, "Defeat!");
						getch();
                        break;
                    }
                    else {
                        mvprintw(18, 1, "%s fainted, choose a new pokemon!", play_arr[cur_pokemon].name.c_str());
                        getch();
                        io_pokemon_list(cur_pokemon, 1);
                        num_fainted = 0;
                    }
                }
            }
            else if(turn_used == 2) {
                play_arr.push_back(encounter);
                mvprintw(18, 1, "%s Success, currently %d pokemon!", encounter.name.c_str(), (int)play_arr.size());
				getch();
                break;
            }
            else if(turn_used == 3) {
                mvprintw(18, 1, "No more room %s flees!", encounter.name.c_str());
				getch();
                break;
            }
        }
        battleScreen(encounter, cur_pokemon, 0);
        choice = getch();
    }
}

void begin() {
    int option;
    vector<trainer_pokemon> initArr;
    
    initArr.push_back(createPokemon());
    initArr.push_back(createPokemon());
    initArr.push_back(createPokemon());
    
    mvprintw(1, 1, "Choose a starting pokemon: ");
    mvprintw(3, 1, "%s, Pokemon's Moves: %s, %s", initArr[0].name.c_str(), initArr[0].move_one.c_str(), initArr[0].move_two.c_str());
    mvprintw(4, 1, "%s, Pokemon's Moves: %s, %s", initArr[1].name.c_str(), initArr[1].move_one.c_str(), initArr[1].move_two.c_str());
    mvprintw(5, 1, "%s, Pokemon's Moves: %s, %s", initArr[2].name.c_str(), initArr[2].move_one.c_str(), initArr[2].move_two.c_str());
    mvprintw(7, 1, "1:  %s, 2: %s, 3: %s.", initArr[0].name.c_str(), initArr[1].name.c_str(), initArr[2].name.c_str());
    
    while(true) {
        option = getch();
        if(option == '1' || option == '2' || option == '3') {
            option -= 49;
            play_arr.push_back(initArr[option]);
            break;
        }
        else {
            mvprintw(9, 1, "Invalid");
			refresh();
        }
    }
    
    mvprintw(9, 1, "You have chosen %s!", play_arr[0].name.c_str());
    mvprintw(11, 1, "Press Enter to start!");
    
    getch();
}

uint32_t move_pc_dir(uint32_t input, pair_t dest) {
    int num_fainted, i, rand_num;
    num_fainted = 0;
    dest[dim_y] = world.pc.pos[dim_y];
    dest[dim_x] = world.pc.pos[dim_x];
    
    switch (input) {
        case 1:
        case 2:
        case 3:
            dest[dim_y]++;
            break;
        case 4:
        case 5:
        case 6:
            break;
        case 7:
        case 8:
        case 9:
            dest[dim_y]--;
            break;
    }
    switch (input) {
        case 1:
        case 4:
        case 7:
            dest[dim_x]--;
            break;
        case 2:
        case 5:
        case 8:
            break;
        case 3:
        case 6:
        case 9:
            dest[dim_x]++;
            break;
        case '>':
            if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
                ter_mart) {
                pokemart();
            }
            if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
                ter_center) {
                center();
            }
            break;
    }
    
    if ((world.cur_map->map[dest[dim_y]][dest[dim_x]] == ter_exit) &&
        (input == 1 || input == 3 || input == 7 || input == 9)) {
        // Exiting diagonally leads to complicated entry into the new map
        // in order to avoid INT_MAX move costs in the destination.
        // Most easily solved by disallowing such entries here.
        return 1;
    }
    
    if (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]) {
        if (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]->npc &&
            world.cur_map->cmap[dest[dim_y]][dest[dim_x]]->npc->defeated) {
            // Some kind of greeting here would be nice
            return 1;
        } else if (world.cur_map->cmap[dest[dim_y]][dest[dim_x]]->npc) {
            io_battle(&world.pc, world.cur_map->cmap[dest[dim_y]][dest[dim_x]]);
            // Not actually moving, so set dest back to PC position
            dest[dim_x] = world.pc.pos[dim_x];
            dest[dim_y] = world.pc.pos[dim_y];
        }
    }
    
    if(world.cur_map->map[dest[dim_y]][dest[dim_x]] == ter_grass) {
        for(i = 0; i < (int)play_arr.size(); i++) {
            if(play_arr[i].cur_hp == 0) {
                num_fainted++;
            }
        }
        if(num_fainted != (int)play_arr.size()) {
            rand_num = rand() % 9;
            if(rand_num == 0) {
                wildPoke();
            }
        }
        else {
            mvprintw(0, 0, "Pokemon need to heal!");
            getch();
			mvprintw(0, 0, "                                                  ");
        }

    }
    
    if (move_cost[char_pc][world.cur_map->map[dest[dim_y]][dest[dim_x]]] ==
        INT_MAX) {
        return 1;
    }
    
    return 0;
}

void io_handle_input(pair_t dest) {
    uint32_t turn_not_consumed;
    int key;
    char choice;
    
    do {
        switch (key = getch()) {
            case '7':
            case 'y':
            case KEY_HOME:
                turn_not_consumed = move_pc_dir(7, dest);
                break;
            case '8':
            case 'k':
            case KEY_UP:
                turn_not_consumed = move_pc_dir(8, dest);
                break;
            case '9':
            case 'u':
            case KEY_PPAGE:
                turn_not_consumed = move_pc_dir(9, dest);
                break;
            case '6':
            case 'l':
            case KEY_RIGHT:
                turn_not_consumed = move_pc_dir(6, dest);
                break;
            case '3':
            case 'n':
            case KEY_NPAGE:
                turn_not_consumed = move_pc_dir(3, dest);
                break;
            case '2':
            case 'j':
            case KEY_DOWN:
                turn_not_consumed = move_pc_dir(2, dest);
                break;
            case '1':
            case 'B':
            case KEY_END:
                turn_not_consumed = move_pc_dir(1, dest);
                break;
            case '4':
            case 'h':
            case KEY_LEFT:
                turn_not_consumed = move_pc_dir(4, dest);
                break;
            case '5':
            case ' ':
            case '.':
            case KEY_B2:
                dest[dim_y] = world.pc.pos[dim_y];
                dest[dim_x] = world.pc.pos[dim_x];
                turn_not_consumed = 0;
                break;
            case '>':
                turn_not_consumed = move_pc_dir('>', dest);
                break;
            case 'Q':
            case 'q':
                dest[dim_y] = world.pc.pos[dim_y];
                dest[dim_x] = world.pc.pos[dim_x];
                world.quit = 1;
                turn_not_consumed = 0;
                break;
                break;
            case 't':
                io_list_trainers();
                turn_not_consumed = 1;
                break;
            case 'b':
                showBag(0);
                choice = getch();
                if(choice == '1') {
                    mvprintw(9, 1, "Invalid option");
                    getch();
                }
				else if(choice == '2') {
					if(trainer_bag.num_potions > 0) {
						heal(choice);
					}
					else {
						mvprintw(9, 1, "Invalid option");
						getch();			
					}
				}
				else if(choice == '3') {
					if(trainer_bag.num_revives > 0) {
						heal(choice);
					}
					else {
						mvprintw(9, 1, "Invalid option");
						getch();			
					}
				}
                else {
                    io_display();
                }
                io_display();
                turn_not_consumed = 1;
                break;
            case 'T':
              /* Teleport the PC to any map in the world.                   */
              io_teleport_world(dest);
              turn_not_consumed = 0;
              break;
            case '+':
                cheater();
                break;
            default:
                /* Also not in the spec.  It's not always easy to figure out what *
                 * key code corresponds with a given keystroke.  Print out any    *
                 * unhandled key here.  Not only does it give a visual error      *
                 * indicator, but it also gives an integer value that can be used *
                 * for that key in this (or other) switch statements.  Printed in *
                 * octal, with the leading zero, because ncurses.h lists codes in *
                 * octal, thus allowing us to do reverse lookups.  If a key has a *
                 * name defined in the header, you can use the name here, else    *
                 * you can directly use the octal value.                          */
                mvprintw(0, 0, "Unbound key: %#o ", key);
                turn_not_consumed = 1;
        }
        refresh();
    } while (turn_not_consumed);
}
