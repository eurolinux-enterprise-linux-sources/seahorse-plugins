
#ifndef __seahorse_marshal_MARSHAL_H__
#define __seahorse_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:STRING,DOUBLE (seahorse-marshal.list:1) */
extern void seahorse_marshal_VOID__STRING_DOUBLE (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* VOID:OBJECT,UINT (seahorse-marshal.list:2) */
extern void seahorse_marshal_VOID__OBJECT_UINT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:OBJECT,POINTER (seahorse-marshal.list:3) */
extern void seahorse_marshal_VOID__OBJECT_POINTER (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* VOID:OBJECT,UINT,POINTER (seahorse-marshal.list:4) */
extern void seahorse_marshal_VOID__OBJECT_UINT_POINTER (GClosure     *closure,
                                                        GValue       *return_value,
                                                        guint         n_param_values,
                                                        const GValue *param_values,
                                                        gpointer      invocation_hint,
                                                        gpointer      marshal_data);

G_END_DECLS

#endif /* __seahorse_marshal_MARSHAL_H__ */

