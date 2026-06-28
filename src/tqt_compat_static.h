/*
 * tqt_compat_static.h - Macros de compat pour le build TQt3 STATIQUE
 *
 * La lib statique native (libtqt-mt.a + headers tq*.h) ne définit QUE les
 * macros TQ_SIGNAL / TQ_SLOT (style natif TQt3). Le code du projet (écrit
 * pour l'environnement Trinity/TDE) utilise l'ancien style SIGNAL / SLOT,
 * qui est une extension fournie par les headers ntq*.h de Trinity.
 *
 * Ce header force-inclus (via -include en build statique) redéfinit les
 * anciennes macros en termes des nouvelles, sans toucher au code source.
 * Le build dynamique TDE n'en a pas besoin (les ntq* le fournissent déjà).
 */
#ifndef TQT_COMPAT_STATIC_H
#define TQT_COMPAT_STATIC_H

/* En build TDE dynamique, les headers Trinity (ntq* / tdecore) incluent
 * transitivement <stdlib.h>, <stdio.h> etc. Les headers natifs tq*.h de la
 * lib statique ne le font pas toujours. On inclut ici les en-têtes C
 * standards dont le code dépend implicitement (system, getenv, popen...). */
#include <stdlib.h>
#include <stdio.h>

/* TQ_SIGNAL / TQ_SLOT sont définis par tqobjectdefs.h (inclus via ntqobject.h).
 * On ne les redéfinit que pour exposer les alias SIGNAL / SLOT attendus. */
#ifndef SIGNAL
  #define SIGNAL(a)  TQ_SIGNAL(a)
#endif
#ifndef SLOT
  #define SLOT(a)    TQ_SLOT(a)
#endif

#endif /* TQT_COMPAT_STATIC_H */
