obj-$(CONFIG_SND_SIMPLE_CARD_UTILS) := simple-card-utils.o

snd-soc-simple-card-objs	:= simple-card.o

ifneq ($(CONFIG_SND_SIMPLE_CARD_UTILS),)
snd-soc-simple-card-objs	+=  simple-card-utils.o
endif

obj-$(CONFIG_SND_SIMPLE_CARD)	+= snd-soc-simple-card.o
