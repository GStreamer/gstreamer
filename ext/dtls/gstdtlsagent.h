/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef gstdtlsagent_h
#define gstdtlsagent_h

#include "gstdtlscertificate.h"

#include <glib-object.h>

G_BEGIN_DECLS

#define ER_TYPE_DTLS_AGENT            (er_dtls_agent_get_type())
#define ER_DTLS_AGENT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), ER_TYPE_DTLS_AGENT, ErDtlsAgent))
#define ER_DTLS_AGENT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), ER_TYPE_DTLS_AGENT, ErDtlsAgentClass))
#define ER_IS_DTLS_AGENT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), ER_TYPE_DTLS_AGENT))
#define ER_IS_DTLS_AGENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), ER_TYPE_DTLS_AGENT))
#define ER_DTLS_AGENT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), ER_TYPE_DTLS_AGENT, ErDtlsAgentClass))

typedef gpointer ErDtlsAgentContext;

typedef struct _ErDtlsAgent        ErDtlsAgent;
typedef struct _ErDtlsAgentClass   ErDtlsAgentClass;
typedef struct _ErDtlsAgentPrivate ErDtlsAgentPrivate;

/*
 * ErDtlsAgent:
 *
 * A context for creating ErDtlsConnections with a ErDtlsCertificate.
 * ErDtlsAgent needs to be constructed with the "certificate" property set.
 */
struct _ErDtlsAgent {
    GObject parent_instance;

    ErDtlsAgentPrivate *priv;
};

struct _ErDtlsAgentClass {
    GObjectClass parent_class;
};

GType er_dtls_agent_get_type(void) G_GNUC_CONST;

/*
 * Returns the certificate used by the agent.
 */
ErDtlsCertificate *er_dtls_agent_get_certificate(ErDtlsAgent *);

/*
 * Returns the certificate used by the agent, in PEM format.
 */
gchar *er_dtls_agent_get_certificate_pem(ErDtlsAgent *self);

/* internal */
void _er_dtls_init_openssl(void);
const ErDtlsAgentContext _er_dtls_agent_peek_context(ErDtlsAgent *);

G_END_DECLS

#endif /* gstdtlsagent_h */
