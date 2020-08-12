TEMPLATE = subdirs

SUBDIRS = engine

EXTENSION_DETAILS = \
    extensions/QContactDeactivated \
    extensions/qcontactdeactivated.h \
    extensions/qcontactdeactivated_impl.h \
    extensions/QContactUndelete \
    extensions/qcontactundelete.h \
    extensions/qcontactundelete_impl.h \
    extensions/QContactOriginMetadata \
    extensions/qcontactoriginmetadata.h \
    extensions/qcontactoriginmetadata_impl.h \
    extensions/QContactStatusFlags \
    extensions/qcontactstatusflags.h \
    extensions/qcontactstatusflags_impl.h

EXTENSION_REQUESTS = \
    extensions/QContactDetailFetchRequest \
    extensions/qcontactdetailfetchrequest.h \
    extensions/qcontactdetailfetchrequest_p.h \
    extensions/qcontactdetailfetchrequest_impl.h \
    extensions/QContactCollectionChangesFetchRequest \
    extensions/qcontactcollectionchangesfetchrequest.h \
    extensions/qcontactcollectionchangesfetchrequest_p.h \
    extensions/qcontactcollectionchangesfetchrequest_impl.h \
    extensions/QContactChangesFetchRequest \
    extensions/qcontactchangesfetchrequest.h \
    extensions/qcontactchangesfetchrequest_p.h \
    extensions/qcontactchangesfetchrequest_impl.h \
    extensions/QContactClearChangeFlagsRequest \
    extensions/qcontactclearchangeflagsrequest.h \
    extensions/qcontactclearchangeflagsrequest_p.h \
    extensions/qcontactclearchangeflagsrequest_impl.h \
    extensions/QContactChangesSaveRequest \
    extensions/qcontactchangessaverequest.h \
    extensions/qcontactchangessaverequest_p.h \
    extensions/qcontactchangessaverequest_impl.h

EXTENSION_PLUGIN_INTERFACES = \
    extensions/displaylabelgroupgenerator.h \
    extensions/twowaycontactsyncadaptor.h \
    extensions/twowaycontactsyncadaptor_impl.h \
    extensions/twowaycontactsyncadapter.h \
    extensions/twowaycontactsyncadapter_impl.h

OTHER_FILES = \
    extensions/contactmanagerengine.h \
    extensions/contactdelta.h \
    extensions/contactdelta_impl.h \
    extensions/qtcontacts-extensions.h \
    extensions/qtcontacts-extensions_impl.h \
    extensions/qtcontacts-extensions_manager_impl.h \
    $$EXTENSION_DETAILS \
    $$EXTENSION_REQUESTS \
    $$EXTENSION_PLUGIN_INTERFACES

