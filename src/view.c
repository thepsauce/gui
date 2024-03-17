#include "gui.h"

int BaseProc(View *view, event_t type, EventInfo *info)
{
	(void) view;
	(void) type;
	(void) info;
	return 0;
}

Class base_class = {
	"!Base",
	BaseProc,
	NULL,
	0,
	NULL
};

Class *first_class = &base_class;
Class *last_class = &base_class;

View base_view = {
	.class = &base_class
};

View *view_Default(void)
{
	return &base_view;
}

Class *class_Create(const char *name, EventProc proc)
{
	Class *class;

	class = union_Alloc(union_Default(), sizeof(*class));
	if (class == NULL) {
		return NULL;
	}
	strncpy(class->name, name, sizeof(class->name));
	class->proc = proc;
	class->properties = NULL;
	class->numProperties = 0;
	class->next = NULL;
	last_class->next = class;
	last_class = class;
	return class;
}

Class *class_Find(const char *name)
{
	for (Class *class = first_class; class != NULL; class = class->next) {
		if (strcmp(class->name, name) == 0) {
			return class;
		}
	}
	return NULL;
}

View *view_Create(const char *className, const Rect *rect)
{
	Class *class;
	Union *uni;
	View *view;

	class = class_Find(className);
	if (class == NULL) {
		PRINT_DEBUG();
		fprintf(stderr, "invalid class name: %s\n", className);
		return NULL;
	}

	uni = union_Alloc(union_Default(), sizeof(*uni));
	if (uni == NULL) {
		return NULL;
	}
	union_Init(uni, SIZE_MAX);
	view = union_Alloc(uni, sizeof(*view));
	if (view == NULL) {
		union_Free(union_Default(), uni);
		return NULL;
	}
	view->class = class;
	view->uni = uni;
	view->rect = *rect;
	view->values = union_Alloc(uni, sizeof(*view->values) *
			class->numProperties);
	if (view->values == NULL) {
		union_Free(uni, view);
		union_Free(union_Default(), uni);
		return NULL;
	}
	for (Uint32 i = 0; i < class->numProperties; i++) {
		view->values[i] = class->properties[i].value;
	}
	view->region = NULL;
	view->prev = NULL;
	view->next = NULL;
	view->child = NULL;
	view->parent = NULL;
	class->proc(view, EVENT_CREATE, NULL);
	return view;
}

int view_SendRecursive(View *view, event_t type, EventInfo *info)
{
	for (; view != NULL; view = view->next) {
		if (view->child != NULL) {
			view_SendRecursive(view->child, type, info);
		}
		view->class->proc(view, type, info);
	}
	return 0;
}

int view_Send(View *view, event_t type, EventInfo *info)
{
	return view->class->proc(view, type, info);
}

Value *view_GetProperty(View *view, type_t type, const char *name)
{
	for (Uint32 i = 0; i < view->class->numProperties; i++) {
		Property *const prop = &view->class->properties[i];
		if (type == prop->type && strcmp(prop->name, name) == 0) {
			return &view->values[i];
		}
	}
	return NULL;
}

bool view_GetBoolProperty(View *view, const char *name)
{
	Value *const prop = view_GetProperty(view, TYPE_BOOL, name);
	if (prop == NULL) {
		return false;
	}
	return prop->b;
}

Uint32 view_GetColorProperty(View *view, const char *name)
{
	Value *const prop = view_GetProperty(view, TYPE_COLOR, name);
	if (prop == NULL) {
		return 0;
	}
	return prop->color;
}

int view_SetParent(View *parent, View *child)
{
	/* isolate the child from... */
	/* ...previous parent */
	if (child->parent != NULL && child->parent->child == child) {
		child->parent->child = child->next;
	}
	child->parent = parent;

	/* ...previous siblings */
	if (child->prev != NULL) {
		child->prev->next = child->next;
	}
	if (child->next != NULL) {
		child->next->prev = child->prev;
	}

	if (parent == NULL) {
		return 0;
	}
	if (parent->child != NULL) {
		parent->child->prev = child;
		child->next = parent->child;
		child->prev = parent->child;
	}
	parent->child = child;
	return 0;
}

void view_Delete(View *view)
{
	Union *uni;

	view_SetParent(NULL, view);
	uni = view->uni;
	union_FreeAll(uni);
	union_Free(union_Default(), uni);
}
