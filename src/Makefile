CC      = gcc
C_FLAGS = -Wall -g
L_FLAGS = -rdynamic -lz -lcrypt -lpthread -lm

O_FILES = act_comm.o act_info.o act_move.o act_obj.o act_wiz.o bit.o mccp.o \
	board.o build.o clan.o comm.o daemon.o db.o angel.o socials.o\
	demon.o drow.o fight.o kav_wiz.o handler.o kav_fight.o kav_info.o \
	interp.o mage.o magic.o mem.o monk.o monk2.o ninja.o olc.o upgrade.o \
	olc_act.o olc_save.o save.o special.o string.o arena.o \
	update.o vamp.o ww.o shapeshifter.o tanarri.o undead_knight.o \
        samurai.o wizutil.o const.o spiderdroid.o lich.o jope.o \
        jobo_king.o jobo_data.o jobo_fight.o jobo_act.o jobo_wiz.o jobo_util.o \
        bot_mgr.o bot_ai.o bot_chat.o bot_gear.o \
        bot_ai_vampire.o bot_ai_monk.o bot_ai_ninja.o bot_ai_demon.o \
        bot_ai_drow.o bot_ai_werewolf.o bot_ai_mage.o bot_ai_tanarri.o \
        bot_ai_angel.o bot_ai_undead_knight.o

merc: $(O_FILES)
	rm -f dystopia
	$(CC)  -o dystopia $(O_FILES) $(L_FLAGS)
	chmod g+w dystopia
	chmod g+w $(O_FILES)

.c.o: merc.h
	$(CC) -c $(C_FLAGS) $<

clean :
	rm -f *.o ; rm -f dystopia
