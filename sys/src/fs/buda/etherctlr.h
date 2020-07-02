typedef struct{
	char*	type;
	int	(*reset)(Ether*);
}Etherctlr;
extern Etherctlr etherctlr[];
extern int netherctlr;
