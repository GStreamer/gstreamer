<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                xmlns:fo="http://www.w3.org/1999/XSL/Format"
                version='1.0'>

<!-- This alters the rendering of URLs.  Let's follow RFC 2396 -->
<!-- guidelines.                                               -->
<xsl:template match="ulink">
 <fo:basic-link external-destination="{@url}"
                xsl:use-attribute-sets="xref.properties">
  <xsl:choose>
   <xsl:when test="count(child::node())=0">
    <xsl:text>&lt;</xsl:text>
    <xsl:value-of select="@url"/>
    <xsl:text>&gt;</xsl:text>
   </xsl:when>
   <xsl:otherwise>
    <xsl:apply-templates/>
   </xsl:otherwise>
  </xsl:choose>
 </fo:basic-link>
 <xsl:if test="count(child::node()) != 0">
  <fo:inline hyphenate="false">
   <xsl:text> at &lt;</xsl:text>
   <xsl:value-of select="@url"/>
   <xsl:text>&gt;</xsl:text>
  </fo:inline>
 </xsl:if>
</xsl:template>

</xsl:stylesheet>

