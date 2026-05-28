#pragma once

/* ==========================================================================
 *  SRS Feature Map - Categorization Facade
 * --------------------------------------------------------------------------
 *  This umbrella header exposes the full SRS 4.2 Categorization pipeline:
 *    REQ-4.2.3.1  [Watched Directory]                  -> scope.h
 *    REQ-4.2.3.2  [Event Detection]                    -> event_detection.h
 *    REQ-4.2.3.3  [Metadata Collection]                -> file_metadata.h
 *    REQ-4.2.3.4  [Primary Categorization]             -> rules.h: firstMatch(), mediaType(), documentType(), subjectOf(), projectOf()
 *    REQ-4.2.3.5  [Text Analysis]                      -> text_extraction.h
 *    REQ-4.2.3.6  [Categorization Metadata Generation] -> rules.h
 *    REQ-4.2.3.7  [Categorization Metadata Storage]    -> storage.h
 *    REQ-4.2.3.8  [Keyword Management]                 -> storage.h
 *    REQ-4.2.3.9  [Keyword Reflection]                 -> rules.h: categorize(); storage.h: addCustomKeywords()
 *    REQ-4.2.3.10 [Categorization Update]              -> storage.h: addKeywordsAndRecategorize(), replaceKeywordsAndRecategorize()
 *    REQ-4.2.3.11 [Search Reflection]                  -> search_reflection.h
 *    REQ-4.2.3.12 [Information Update]                 -> event_detection.h: fileStateChanged(); storage.h: applyCategorizationEvent()
 *    REQ-4.2.3.13 [Exception Handling]                 -> metadata/text status
 * --------------------------------------------------------------------------
 *  This file should be included when callers need to use the categorization
 *  feature described by SRS 4.2.
 * ========================================================================== */
#include "types.h"
#include "text_utils.h"
#include "collection_utils.h"
#include "file_metadata.h"
#include "scope.h"
#include "event_detection.h"
#include "text_extraction.h"
#include "rules.h"
#include "search_reflection.h"
#include "storage.h"